// SPDX-License-Identifier: BSD-3-Clause

#include "arrays.h"
#include "bindings.h"
#include "common.h"
#include "options.h"
#include "trampolines.h"

#include "terraingen/sources/diamond_square.h"
#include "terraingen/sources/field.h"
#include "terraingen/sources/flat_source.h"
#include "terraingen/sources/geotiff_source.h"
#include "terraingen/sources/raster_source.h"
#include "terraingen/sources/source.h"
#include "terraingen/sources/source_chain.h"
#include "terraingen/sources/synthetic_source.h"
#include "terraingen/sources/terrain_base.h"
#include "terraingen/sources/upsampling.h"
#include "terraingen/util/parallel.h"

#include <nanobind/operators.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace terraingen::python {

namespace {

/// @brief A source written in Python: `grid()` and `height_at(x, y, spacing)` are required;
/// `fill(heights, spacing, threads)` fills a whole raster at once, and `reference_point()` gives
/// (x, y) noise is measured from.
class PySource : public Source {
  public:
    NB_TRAMPOLINE(Source);

    RasterGrid Grid() const override {
        return CallRequiredAs<RasterGrid>(nb_trampoline, "grid");
    }

    Vec2d ReferencePoint() const override {
        return CallOverride<Vec2d>(nb_trampoline, "reference_point").value_or(Vec2d{});
    }

    void Fill(Raster<float>& heights, double spacing, unsigned threads) const override {
        if (CallOverrideIgnoringResult(nb_trampoline, "fill", ByReference<Raster<float>>{heights},
                                       spacing, threads)) {
            return;
        }
        ParallelFor(heights.Height(), threads, [&](std::size_t row) {
            for (std::size_t col = 0; col < heights.Width(); ++col) {
                const Vec2d center = heights.CellCenter(col, row);
                heights(col, row) = HeightAt(center.x, center.y, spacing);
            }
        });
    }

    float HeightAt(double x, double y, double spacing) const override {
        return CallRequiredAs<float>(nb_trampoline, "height_at", x, y, spacing);
    }

    double ResolutionAt(double x, double y) const override {
        return CallOverride<double>(nb_trampoline, "resolution_at", x, y)
            .value_or(std::numeric_limits<double>::infinity());
    }
};

/// @brief An upsampler written in Python: `height_at(posts, x, y, spacing)` is required;
/// `fill(posts, heights, spacing, threads)`, `reach()` and `fit_to(posts)` are optional.
class PyUpsampler : public Upsampler {
  public:
    NB_TRAMPOLINE(Upsampler);

    float HeightAt(const PostLattice& posts, double x, double y, double spacing) const override {
        return CallRequiredAs<float>(nb_trampoline, "height_at",
                                     ByReference<const PostLattice>{posts}, x, y, spacing);
    }

    void Fill(const PostLattice& posts, Raster<float>& heights, double spacing,
              unsigned threads) const override {
        if (CallOverrideIgnoringResult(nb_trampoline, "fill", ByReference<const PostLattice>{posts},
                                       ByReference<Raster<float>>{heights}, spacing, threads)) {
            return;
        }
        Upsampler::Fill(posts, heights, spacing, threads);
    }

    int Reach() const override {
        return CallOverride<int>(nb_trampoline, "reach").value_or(Upsampler::Reach());
    }

    std::shared_ptr<const Upsampler> FitTo(const PostLattice& posts) const override {
        return CallOverride<std::shared_ptr<Upsampler>>(nb_trampoline, "fit_to",
                                                        ByReference<const PostLattice>{posts})
            .value_or(nullptr);
    }
};

/// A raster of heights from a 2D array of shape (height, width).
Raster<float> HeightsFromArray(const CpuMatrix<float>& array, const GeoTransform& transform,
                               std::optional<float> noData) {
    Raster<float> raster(array.shape(1), array.shape(0));
    std::copy(array.data(), array.data() + array.size(), raster.Values().begin());
    raster.SetTransform(transform);
    raster.SetNoData(noData);
    return raster;
}

/// Raster source options from the keyword arguments.
RasterSourceOptions SamplingOptions(std::optional<double> effectivePixelSize,
                                    std::shared_ptr<Upsampler> upsampler) {
    RasterSourceOptions options;
    options.effectivePixelSize = effectivePixelSize;
    options.upsampler = std::move(upsampler);
    return options;
}

/// `fill` with a spacing, or the grid's own without one.
void FillAt(const Source& source, Raster<float>& heights, std::optional<double> spacing,
            unsigned threads) {
    source.Fill(heights, spacing ? *spacing : RasterGrid::FromRaster(heights).PixelSize(), threads);
}

void BindSourceBase(nb::module_& m) {
    nb::class_<Source, PySource>(
        m, "Source",
        "Where a terrain's heights come from, over its grid. Subclass it and implement `grid()` "
        "(a RasterGrid) and `height_at(x, y, spacing)`, and optionally `fill(heights, spacing, "
        "threads)`, `reference_point()` and `resolution_at(x, y)`, for a source of your own. "
        "Spacings are sample spacings in the source's units.")
        .def(nb::init<>())
        .def("grid", &Source::Grid,
             "The grid the source covers; one without cells for a source without bounds.")
        .def("height_at", &Source::HeightAt, "x"_a, "y"_a, "spacing"_a,
             "The height at (x, y) at a sample spacing, NaN where the source has no data.")
        .def("resolution_at", &Source::ResolutionAt, "x"_a, "y"_a,
             "The cell size in CRS units of the data giving the height at (x, y), before any "
             "upsampling, or its effective pixel size: the scale below which the source resolves "
             "nothing. Infinity where no measured data gives it, as for synthetic noise.")
        .def("reference_point", &Source::ReferencePoint)
        .def("fill", &FillAt, "heights"_a, "spacing"_a = nb::none(), "threads"_a = 0u,
             nb::call_guard<nb::gil_scoped_release>(),
             "Sets every cell of `heights` at a sample spacing, by default its cell size, on "
             "`threads` threads (0 for every hardware thread).")
        .def_prop_ro(
            "bounded", [](const Source& source) { return BoundedGrid(source).has_value(); },
            "Whether the source has bounds: a grid with cells.")
        .def_prop_ro("width", [](const Source& source) { return source.Grid().width; })
        .def_prop_ro("height", [](const Source& source) { return source.Grid().height; })
        .def_prop_ro("transform", [](const Source& source) { return source.Grid().transform; });
    nb::class_<TerrainBase>(m, "TerrainBase",
                            "What a terrain's layers are built on: the `surface` they lie on and "
                            "the `source` whose heights they add to, if any. Every layer takes one "
                            "as `base`; a Surface or a Source alone converts to one.")
        .def(nb::init<Surface, std::shared_ptr<const Source>>(), "surface"_a = Surface{},
             "source"_a.none() = nb::none())
        .def(nb::init_implicit<Surface>())
        .def("__init__",
             [](TerrainBase* self, std::shared_ptr<const Source> source) {
                 new (self) TerrainBase(Surface{}, std::move(source));
             })
        .def_rw("surface", &TerrainBase::surface)
        .def_rw("source", &TerrainBase::source, nb::arg("value").none())
        .def("reference_point", &TerrainBase::ReferencePoint,
             "Where noise on a plane is measured from: the source's reference point.");
    nb::implicitly_convertible<std::shared_ptr<const Source>, TerrainBase>();
}

/// The posts [`col0`, `col0` + `width`) x [`row0`, `row0` + `height`) as an array of shape
/// (height, width).
nb::object ReadPosts(const PostLattice& posts, std::int64_t col0, std::int64_t row0,
                     std::size_t width, std::size_t height) {
    std::vector<float> values(width * height);
    posts.Read(col0, row0, width, height, Span<float>(values));
    return nb::cast(OwnedArray(std::move(values), {height, width}));
}

void BindPosts(nb::module_& m) {
    nb::class_<PostLattice>(
        m, "PostLattice",
        "A source's posts: heights on a regular lattice, post (col, row) at the center of cell "
        "(col, row) of a grid with `transform()`.")
        .def("transform", &PostLattice::Transform)
        .def(
            "extent",
            [](const PostLattice& posts)
                -> std::optional<std::tuple<std::size_t, std::size_t, std::size_t, std::size_t>> {
                const std::optional<PixelWindow> extent = posts.Extent();
                if (!extent) {
                    return std::nullopt;
                }
                return std::tuple{extent->colBegin, extent->colEnd, extent->rowBegin,
                                  extent->rowEnd};
            },
            "The posts held as (col_begin, col_end, row_begin, row_end); None without bounds.")
        .def("read", &ReadPosts, "col0"_a, "row0"_a, "width"_a, "height"_a,
             "The posts from (col0, row0) as a float32 array of shape (height, width), NaN where "
             "a post has no data; posts beyond the lattice repeat its edge.");
    nb::class_<RasterPosts, PostLattice>(m, "RasterPosts", "The cells of a raster as posts.")
        .def(
            "__init__",
            [](RasterPosts* self, const Raster<float>& raster) {
                auto shared = std::make_shared<const Raster<float>>(raster);
                new (self) RasterPosts(shared, RasterPlacement::Whole(*shared));
            },
            "raster"_a);
    m.def("post_spacing", &PostSpacing, "transform"_a,
          "The spacing of a lattice's posts: the longer side of a cell of `transform`.");
}

void BindKernelUpsampler(nb::module_& m) {
    nb::class_<Upsampler, PyUpsampler>(
        m, "Upsampler",
        "How a source's posts become heights at any point and sample spacing. Subclass it and "
        "implement `height_at(posts, x, y, spacing)`, and optionally `fill(posts, heights, "
        "spacing, threads)`, `reach()` (posts read either side of a point) and `fit_to(posts)` (an "
        "upsampler with settings estimated from the posts, or None), for one of your own; "
        "`register_upsampler` lets configurations name it.")
        .def(nb::init<>())
        .def("height_at", &Upsampler::HeightAt, "posts"_a, "x"_a, "y"_a, "spacing"_a)
        .def(
            "fill",
            [](const Upsampler& upsampler, const PostLattice& posts, Raster<float>& heights,
               double spacing,
               unsigned threads) { upsampler.Fill(posts, heights, spacing, threads); },
            "posts"_a, "heights"_a, "spacing"_a, "threads"_a = 0u,
            nb::call_guard<nb::gil_scoped_release>(),
            "Sets each cell of `heights` to `height_at` its center, on `threads` threads (0 for "
            "every hardware thread).")
        .def("reach", &Upsampler::Reach)
        .def(
            "fit_to",
            [](const Upsampler& upsampler, const PostLattice& posts) {
                return Mutable(upsampler.FitTo(posts));
            },
            "posts"_a);
    nb::class_<KernelUpsampler, Upsampler>(
        m, "KernelUpsampler",
        "Interpolation of the posts with a kernel at every spacing: cubic, the default, gives the "
        "same heights as `sample_raster`.")
        .def(nb::init<ResampleMethod>(), "method"_a = ResampleMethod::Cubic);
}

void BindDiamondSquare(nb::module_& m) {
    BindEnum<DiamondSquareInterp>(m, "DiamondSquareInterp",
                                  "How diamond-square fills new nodes and reads between them.");
    auto options = nb::class_<DiamondSquareOptions>(
        m, "DiamondSquareOptions",
        "The fractal diamond-square refines posts with: `fractal_dimension` and `scale` (meters; "
        "None estimates them from the posts), `fit_window` (side in posts of the overlapping "
        "windows a None scale is fitted over, so each region follows its own roughness; 0 fits "
        "one scale to the posts), `interp`, `min_pixel_size` (posts' units, 0 for no limit) and "
        "`seed`.");
    BindKwargsInit<DiamondSquareOptions>(options);
    options.def_rw("fractal_dimension", &DiamondSquareOptions::fractalDimension)
        .def_rw("scale", &DiamondSquareOptions::scale)
        .def_rw("fit_window", &DiamondSquareOptions::fitWindow)
        .def_rw("interp", &DiamondSquareOptions::interp)
        .def_rw("min_pixel_size", &DiamondSquareOptions::minPixelSize)
        .def_rw("seed", &DiamondSquareOptions::seed)
        .def("validate", &ValidateDiamondSquareOptions, kValidateDoc)
        .def(nb::self == nb::self);
    m.def("validate_diamond_square_options", &ValidateDiamondSquareOptions, "options"_a,
          kValidateDoc);
    nb::class_<MidpointRoughness>(m, "MidpointRoughness",
                                  "The displacement scale and fractal dimension a raster shows.")
        .def_ro("scale", &MidpointRoughness::scale)
        .def_ro("fractal_dimension", &MidpointRoughness::fractalDimension);
    m.def("estimate_midpoint_roughness", &EstimateMidpointRoughness, "heights"_a,
          "The scale and fractal dimension of a raster's heights on its own cells, by medians so "
          "that a few large features such as craters do not set them.");
    m.def("estimate_midpoint_scale", &EstimateMidpointScale, "heights"_a,
          "The displacement scale of a raster's heights on its own cells: 1.4826 times the median "
          "absolute difference of each cell and the mean of its four neighbors.");
    m.attr("MAX_DIAMOND_SQUARE_FIT_WINDOW") = kMaxDiamondSquareFitWindow;
    nb::class_<DiamondSquareUpsampler, Upsampler>(
        m, "DiamondSquareUpsampler",
        "Random midpoint displacement (diamond-square) of the posts: each level halves the node "
        "spacing down to the sample spacing, adding fractal detail without changing the posts.")
        .def(nb::init<DiamondSquareOptions>(), "options"_a = DiamondSquareOptions{})
        .def_prop_ro("options", &DiamondSquareUpsampler::Options);
}

void BindUpsamplerRegistry(nb::module_& m) {
    m.def(
        "default_upsampler", [] { return Mutable(DefaultUpsampler()); },
        "The upsampler sources use unless given one: cubic interpolation.");
    m.def("kernel_upsampler_names", &KernelUpsamplerNames,
          "The names configurations give the kernel upsamplers.");
    m.def(
        "register_upsampler",
        [](const std::string& name, nb::object factory) {
            const auto* held = new nb::object(std::move(factory));
            RegisterUpsampler(name, [held](const std::string& settings) {
                const nb::gil_scoped_acquire gil;
                const nb::dict keywords =
                    nb::cast<nb::dict>(nb::module_::import_("json").attr("loads")(settings));
                const nb::object made = (*held)(**keywords);
                return std::shared_ptr<const Upsampler>(nb::cast<std::shared_ptr<Upsampler>>(made));
            });
        },
        "name"_a, "factory"_a,
        "Registers an upsampler type under `name`, so that configurations can name it as "
        "\"upsampling\": \"name\" or {\"type\": \"name\", ...settings}: `factory(**settings)`, "
        "such as an Upsampler subclass, makes it. Replaces a type registered under the name "
        "before.");
    m.def("registered_upsampler_names", &RegisteredUpsamplerNames,
          "The names upsampler types are registered under.");
}

void BindGeneratedSources(nb::module_& m) {
    nb::class_<FlatSource, Source>(m, "FlatSource",
                                   "A level datum at `elevation` over a `width` x `height` grid of "
                                   "`pixel_size` meters centered on `center`.")
        .def(nb::init<std::size_t, std::size_t, double, float, Vec2d, const Surface&>(), "width"_a,
             "height"_a, "pixel_size"_a, "elevation"_a = 0.0f, "center"_a = Vec2d{},
             "surface"_a = Surface{},
             "On a body `center` is (longitude, latitude) in degrees and `pixel_size` stays in "
             "meters, converted to degrees along a meridian.");
    nb::class_<SyntheticSource, Source>(
        m, "SyntheticSource",
        "Heights generated from noise without bounds: `elevation` meters plus the `fractals` "
        "(none is flat), evaluated at every point, or with `pixel_size` (meters) "
        "sampled at posts that `upsampler` refines (cubic when None).")
        .def(
            "__init__",
            [](SyntheticSource* self, std::vector<Fractal> fractals, float elevation,
               std::optional<double> pixelSize, std::shared_ptr<Upsampler> upsampler,
               const Surface& surface) {
                new (self) SyntheticSource(std::move(fractals), elevation, pixelSize,
                                           std::move(upsampler), surface);
            },
            "fractals"_a = std::vector<Fractal>{}, "elevation"_a = 0.0f,
            "pixel_size"_a = nb::none(), "upsampler"_a = nb::none(), "surface"_a = Surface{})
        .def_prop_ro("fractals", &SyntheticSource::Fractals)
        .def_prop_ro("pixel_size", [](const SyntheticSource& s) { return s.PixelSize(); })
        .def_prop_ro(
            "upsampler", [](const SyntheticSource& s) { return Mutable(s.UpsamplerInUse()); },
            "The upsampler in use, fitted to the posts; None without a pixel size.");
}

void BindRasterSources(nb::module_& m) {
    nb::class_<GeoTiffSource, Source>(
        m, "GeoTiffSource",
        "Heights from band `band` of a GeoTIFF, scaled (height * scale + offset), its cells "
        "refined by `upsampler` (cubic when None). A projected file suits a plane terrain and one "
        "in longitude and latitude a terrain on a body, its crop and cell sizes in degrees. "
        "The file is decoded a tile or strip at a time as heights are asked of them and kept in "
        "a cache of at most `cache_bytes`, so a small area of a file too large for memory reads "
        "only the blocks around it. With `crop` (min_x, min_y, max_x, max_y) it covers only the "
        "cells meeting that box, giving the same heights there as without it. "
        "`effective_pixel_size` is the cell size the heights truly resolve, for a DEM upsampled "
        "before it was saved.")
        .def(
            "__init__",
            [](GeoTiffSource* self, const std::filesystem::path& path, std::size_t band,
               std::optional<double> scale, std::optional<double> offset,
               std::optional<Bounds2d> crop, std::optional<double> effectivePixelSize,
               std::shared_ptr<Upsampler> upsampler, std::size_t cacheBytes) {
                GeoTiffSourceOptions options;
                options.band = band;
                options.scale = scale;
                options.offset = offset;
                options.crop = crop;
                options.effectivePixelSize = effectivePixelSize;
                options.upsampler = std::move(upsampler);
                options.cacheBytes = cacheBytes;
                new (self) GeoTiffSource(path, options);
            },
            "path"_a, "band"_a = 0, "scale"_a = nb::none(), "offset"_a = nb::none(),
            "crop"_a = nb::none(), "effective_pixel_size"_a = nb::none(),
            "upsampler"_a = nb::none(), "cache_bytes"_a = kDefaultGeoTiffCacheBytes);
    nb::class_<RasterSource, Source>(
        m, "RasterSource",
        "Heights from a raster held in memory, used as given with its no-data value, its cells "
        "refined by `upsampler` (cubic when None); `effective_pixel_size` (CRS units) is the "
        "cell size the heights truly resolve. `keys` is the projection outputs are written with; "
        "geographic keys on columns spanning 360 degrees wrap the raster in longitude.")
        .def(
            "__init__",
            [](RasterSource* self, const Raster<float>& heights,
               std::optional<double> effectivePixelSize, std::shared_ptr<Upsampler> upsampler,
               const GeoKeys& keys) {
                new (self) RasterSource(
                    heights, SamplingOptions(effectivePixelSize, std::move(upsampler)), keys);
            },
            "heights"_a, "effective_pixel_size"_a = nb::none(), "upsampler"_a = nb::none(),
            "keys"_a = GeoKeys{})
        .def(
            "__init__",
            [](RasterSource* self, const CpuMatrix<float>& heights, const GeoTransform& transform,
               std::optional<float> noData, std::optional<double> effectivePixelSize,
               std::shared_ptr<Upsampler> upsampler, const GeoKeys& keys) {
                new (self)
                    RasterSource(HeightsFromArray(heights, transform, noData),
                                 SamplingOptions(effectivePixelSize, std::move(upsampler)), keys);
            },
            "heights"_a, "transform"_a = GeoTransform{}, "no_data"_a = nb::none(),
            "effective_pixel_size"_a = nb::none(), "upsampler"_a = nb::none(), "keys"_a = GeoKeys{},
            "From a float32 array of shape (height, width), copied, placed by `transform`, with "
            "`no_data` marking cells without data.")
        .def_prop_ro(
            "upsampler", [](const RasterSource& s) { return Mutable(s.UpsamplerInUse()); },
            "The upsampler in use, fitted to the raster.");
    nb::class_<SourceChain, Source>(
        m, "SourceChain",
        "Several sources tried in order, each filling where those before it have no data; the "
        "grid is the first's. `seams` (ChainSeams) sets how each source is matched to the gaps "
        "of those before it.")
        .def(
            "__init__",
            [](SourceChain* self, const std::vector<std::shared_ptr<Source>>& sources,
               const ChainSeams& seams) {
                new (self) SourceChain({sources.begin(), sources.end()}, seams);
            },
            "sources"_a, "seams"_a = ChainSeams{})
        .def_prop_ro("seams", &SourceChain::Seams,
                     "How each source is joined to the gaps of those before it.");
}

/// A field written in Python: `value_at(x, y)` and `max()` are required.
class PyField : public SurfaceField {
  public:
    NB_TRAMPOLINE(SurfaceField);

    double ValueAt(double x, double y) const override {
        return CallRequiredAs<double>(nb_trampoline, "value_at", x, y);
    }

    double Max() const override {
        return CallRequiredAs<double>(nb_trampoline, "max");
    }
};

void BindFields(nb::module_& m) {
    nb::class_<SurfaceField, PyField>(
        m, "SurfaceField",
        "A scalar quantity over a surface, read at surface coordinates (meters on a plane, "
        "longitude and latitude in degrees on a sphere), such as a density map or a catalog's "
        "completeness limit. Subclass it and implement `value_at(x, y)` (NaN where it has no "
        "value) and `max()` for a field of your own.")
        .def(nb::init<>())
        .def("value_at", &SurfaceField::ValueAt, "x"_a, "y"_a)
        .def("max", &SurfaceField::Max);
    nb::class_<ConstantField, SurfaceField>(m, "ConstantField", "The same `value` everywhere.")
        .def(nb::init<double>(), "value"_a);
    nb::class_<MinimumField, SurfaceField>(
        m, "MinimumField",
        "The smaller of two fields at each point, the one with a value where the other is NaN, "
        "such as a catalog's completeness limit and `source_resolved_limit`.")
        .def(nb::init<std::shared_ptr<const SurfaceField>, std::shared_ptr<const SurfaceField>>(),
             "a"_a, "b"_a);
    auto options = nb::class_<RasterFieldOptions>(
        m, "RasterFieldOptions",
        "How a raster field is read: `method` (nearest or bilinear), `scale` multiplying every "
        "cell, the `fallback` where the raster has no value (NaN for none), and whether x is a "
        "`periodic_longitude`.");
    BindKwargsInit<RasterFieldOptions>(options);
    options.def_rw("method", &RasterFieldOptions::method)
        .def_rw("scale", &RasterFieldOptions::scale)
        .def_rw("fallback", &RasterFieldOptions::fallback)
        .def_rw("periodic_longitude", &RasterFieldOptions::periodicLongitude);
    m.def("validate_raster_field_options", &ValidateRasterFieldOptions, "options"_a, kValidateDoc);
    nb::class_<RasterField, SurfaceField>(
        m, "RasterField", "A field interpolated from a raster in the surface's coordinates.")
        .def(nb::init<Raster<float>, RasterFieldOptions>(), "raster"_a,
             "options"_a = RasterFieldOptions{})
        .def_prop_ro("values", &RasterField::Values)
        .def_prop_ro("options", &RasterField::Options);
    m.def("read_raster_field", &ReadRasterField, "path"_a, "options"_a = RasterFieldOptions{},
          "band"_a = 0, "A field read from one band of a GeoTIFF in the surface's coordinates.");
}

}  // namespace

void BindSources(nb::module_& m) {
    BindSourceBase(m);
    BindPosts(m);
    BindKernelUpsampler(m);
    BindDiamondSquare(m);
    BindUpsamplerRegistry(m);
    BindGeneratedSources(m);
    BindRasterSources(m);
    BindFields(m);
}

}  // namespace terraingen::python
