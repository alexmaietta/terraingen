// SPDX-License-Identifier: BSD-3-Clause

#include "arrays.h"
#include "bindings.h"
#include "common.h"
#include "options.h"
#include "trampolines.h"

#include "terraingen/layers/field.h"
#include "terraingen/sources/diamond_square.h"
#include "terraingen/sources/flat_source.h"
#include "terraingen/sources/geotiff_source.h"
#include "terraingen/sources/raster_source.h"
#include "terraingen/sources/sample_block.h"
#include "terraingen/sources/shape_model_source.h"
#include "terraingen/sources/source.h"
#include "terraingen/sources/source_chain.h"
#include "terraingen/sources/sources_fields.h"
#include "terraingen/sources/synthetic_source.h"
#include "terraingen/sources/upsampling.h"

#include <nanobind/operators.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace terraingen::python {

namespace {

/// Discards what a Python override returns.
void Ignore(nb::handle /*result*/) {}

/// @brief A source written in Python: `grid()` and `fill_samples(samples, spacing, threads)` are
/// required; `reference_point()` gives (x, y) noise is measured from.
class PySource : public Source {
  public:
    NB_TRAMPOLINE(Source);

    RasterGrid Grid() const override {
        return CallRequiredAs<RasterGrid>(nb_trampoline, "grid");
    }

    Vec2d ReferencePoint() const override {
        return CallOverride<Vec2d>(nb_trampoline, "reference_point").value_or(Vec2d{});
    }

    double ResolutionAt(double x, double y) const override {
        return CallOverride<double>(nb_trampoline, "resolution_at", x, y)
            .value_or(std::numeric_limits<double>::infinity());
    }

    double PostSpacingAt(double x, double y) const override {
        return CallOverride<double>(nb_trampoline, "post_spacing_at", x, y)
            .value_or(std::numeric_limits<double>::infinity());
    }

    std::shared_ptr<const Source> Reseeded(std::uint64_t seed) const override {
        return CallOverride<std::shared_ptr<Source>>(nb_trampoline, "reseeded", seed)
            .value_or(nullptr);
    }

  protected:
    void FillSamples(SampleBlock& samples, double spacing, unsigned threads) const override {
        CallRequired(nb_trampoline, "fill_samples", &Ignore, ByReference<SampleBlock>{samples},
                     spacing, threads);
    }
};

/// @brief An upsampler written in Python: `fill_samples(posts, samples, spacing, threads)` is
/// required; `reach()` and `fit_to(posts)` are optional.
class PyUpsampler : public Upsampler {
  public:
    NB_TRAMPOLINE(Upsampler);

    int Reach() const override {
        return CallOverride<int>(nb_trampoline, "reach").value_or(Upsampler::Reach());
    }

    std::shared_ptr<const Upsampler> FitTo(const PostLattice& posts) const override {
        return CallOverride<std::shared_ptr<Upsampler>>(nb_trampoline, "fit_to",
                                                        ByReference<const PostLattice>{posts})
            .value_or(nullptr);
    }

    std::shared_ptr<const Upsampler> Reseeded(std::uint64_t seed) const override {
        return CallOverride<std::shared_ptr<Upsampler>>(nb_trampoline, "reseeded", seed)
            .value_or(nullptr);
    }

  protected:
    void FillSamples(const PostLattice& posts, SampleBlock& samples, double spacing,
                     unsigned threads) const override {
        CallRequired(nb_trampoline, "fill_samples", &Ignore, ByReference<const PostLattice>{posts},
                     ByReference<SampleBlock>{samples}, spacing, threads);
    }
};

/// A writable view of a sample block's heights, valid while the block is.
using HeightsView = nb::ndarray<nb::numpy, float, nb::ndim<1>, nb::c_contig>;

void BindSampleBlock(nb::module_& m) {
    nb::class_<SampleBlock>(
        m, "SampleBlock",
        "Heights and where they are, which `fill_samples` of a Source, Upsampler or Ground "
        "subclass sets: every cell of a raster or scattered points. Valid only during that call.")
        .def("size", &SampleBlock::Size, "The number of samples.")
        .def("is_grid", &SampleBlock::IsGrid, "Whether the samples are a raster's cells.")
        .def("grid", &SampleBlock::Grid, nb::rv_policy::reference_internal,
             "The raster of a grid block, or None for points.")
        .def("position", &SampleBlock::Position, "i"_a,
             "The position of sample `i`: a cell center, or a point.")
        .def(
            "positions",
            [](const SampleBlock& samples) {
                std::vector<double> values;
                for (const Vec2d& p : samples.Positions()) {
                    values.push_back(p.x);
                    values.push_back(p.y);
                }
                return OwnedArray(std::move(values), {samples.Size(), 2});
            },
            "The position of every sample as an array of shape (n, 2), a grid's cell centers row "
            "by row.")
        .def(
            "heights",
            [](nb::handle self) {
                const Span<float> heights = nb::cast<SampleBlock&>(self).Heights();
                return HeightsView(heights.data(), {heights.size()}, self);
            },
            "Every height as a writable float32 array, a grid's row by row.")
        .def("is_no_data", &SampleBlock::IsNoData, "i"_a,
             "Whether sample `i` holds no data: NaN, or a grid's no-data value.")
        .def("bounds", &SampleBlock::Bounds, "The smallest box holding every sample.");
}

/// A raster of heights from a 2D array of shape (height, width).
Raster<float> HeightsFromArray(const CpuMatrix<float>& array, const GeoTransform& transform,
                               std::optional<float> noData) {
    Raster<float> raster(RasterGrid{array.shape(1), array.shape(0), transform});
    std::copy(array.data(), array.data() + array.size(), raster.Values().begin());
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
    source.Fill(heights, spacing ? *spacing : heights.Grid().PixelSize(), threads);
}

/// `fill` of `grid` into an array of shape (height, width), at a spacing or the grid's own.
/// @throws std::invalid_argument for an array of another shape.
void FillArray(const Source& source, const RasterGrid& grid, CpuOutMatrix<float> heights,
               std::optional<double> spacing, unsigned threads) {
    if (heights.shape(0) != grid.height || heights.shape(1) != grid.width) {
        throw std::invalid_argument("Expected a float32 array of shape (" +
                                    std::to_string(grid.height) + ", " +
                                    std::to_string(grid.width) + ")");
    }
    const nb::gil_scoped_release release;
    source.Fill(grid, Span<float>(heights.data(), heights.size()),
                spacing ? *spacing : grid.PixelSize(), threads);
}

/// `heights_at` the rows of `points`, an array of shape (n, 2), as a float32 array.
nb::object HeightsAtArray(const Source& source, const CpuMatrix<double>& points, double spacing) {
    const Span<const Vec2d> positions = PointSpan(points);
    std::vector<float> heights(positions.size());
    {
        const nb::gil_scoped_release release;
        source.HeightsAt(positions, Span<float>(heights), spacing);
    }
    return Column(std::move(heights));
}

void BindSourceBase(nb::module_& m) {
    BindSampleBlock(m);
    nb::class_<Source, PySource>(
        m, "Source",
        "Where a terrain's heights come from, over its grid. Subclass it and implement `grid()` "
        "(a RasterGrid) and `fill_samples(samples, spacing, threads)`, which sets every height of "
        "a SampleBlock (NaN where there is no data) and serves grids and points alike, and "
        "optionally `reference_point()`, `resolution_at(x, y)` and `post_spacing_at(x, y)`, for "
        "a source of your own. Spacings are sample spacings in the source's units.")
        .def(nb::init<>())
        .def("grid", &Source::Grid,
             "The grid the source covers; one without cells for a source without bounds.")
        .def("height_at", &Source::HeightAt, "x"_a, "y"_a, "spacing"_a,
             "The height at (x, y) at a sample spacing, NaN where the source has no data.")
        .def("heights_at", &HeightsAtArray, "points"_a, "spacing"_a,
             "The heights at `points`, an array of shape (n, 2), at a sample spacing, as a float32 "
             "array; each equals `height_at` there. Batches of points are much faster than one "
             "call per point.")
        .def("resolution_at", &Source::ResolutionAt, "x"_a, "y"_a,
             "The cell size in CRS units of the measured data giving the height at (x, y), or its "
             "effective pixel size: the scale below which the source resolves nothing. Infinity "
             "where no measured data gives it, as for synthetic noise, and where the source has "
             "no height.")
        .def("post_spacing_at", &Source::PostSpacingAt, "x"_a, "y"_a,
             "The spacing in CRS units of the posts giving the height at (x, y) before any "
             "upsampling, measured or generated. Infinity where no posts give it, as for noise "
             "evaluated at every point, and where the source has no height.")
        .def("reference_point", &Source::ReferencePoint)
        .def("reseeded", &Source::Reseeded, "seed"_a,
             "A copy of the source whose seeds draw from `seed`, sharing any data it read; None "
             "when the copy would equal the source, as for measured data.")
        .def("fill", &FillAt, "heights"_a, "spacing"_a = nb::none(), "threads"_a = 0u,
             nb::call_guard<nb::gil_scoped_release>(),
             "Sets every cell of `heights` at a sample spacing, by default its cell size, on "
             "`threads` threads (0 for every hardware thread).")
        .def("fill", &FillArray, "grid"_a, "heights"_a, "spacing"_a = nb::none(), "threads"_a = 0u,
             "Sets `heights`, a float32 array of shape (grid.height, grid.width), to the cells of "
             "`grid` at a sample spacing, by default its cell size, NaN where the source has no "
             "data.")
        .def_prop_ro(
            "bounded", [](const Source& source) { return BoundedGrid(source).has_value(); },
            "Whether the source has bounds: a grid with cells.")
        .def_prop_ro("width", [](const Source& source) { return source.Grid().width; })
        .def_prop_ro("height", [](const Source& source) { return source.Grid().height; })
        .def_prop_ro("transform", [](const Source& source) { return source.Grid().transform; });
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
        "How a source's posts become heights at any point and sample spacing; a post keeps its "
        "own height at the posts' spacing or coarser. Subclass it and implement "
        "`fill_samples(posts, samples, spacing, threads)`, which sets every height of a "
        "SampleBlock, and optionally `reach()` (posts read either side of a point) and "
        "`fit_to(posts)` (an upsampler with settings estimated from the posts, or None), for one "
        "of your own; `register_upsampler` lets configurations name it.")
        .def(nb::init<>())
        .def("height_at", &Upsampler::HeightAt, "posts"_a, "x"_a, "y"_a, "spacing"_a,
             "The height at (x, y) from `posts` at a sample spacing.")
        .def(
            "heights_at",
            [](const Upsampler& upsampler, const PostLattice& posts,
               const CpuMatrix<double>& points, double spacing) {
                const Span<const Vec2d> positions = PointSpan(points);
                std::vector<float> heights(positions.size());
                {
                    const nb::gil_scoped_release release;
                    upsampler.HeightsAt(posts, positions, Span<float>(heights), spacing);
                }
                return Column(std::move(heights));
            },
            "posts"_a, "points"_a, "spacing"_a,
            "`height_at` each row of `points`, an array of shape (n, 2), as a float32 array.")
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
            "posts"_a)
        .def("reseeded", &Upsampler::Reseeded, "seed"_a,
             "A copy of the upsampler whose seeds draw from `seed`; None for one without seeds.");
    nb::class_<KernelUpsampler, Upsampler>(
        m, "KernelUpsampler",
        "Interpolation of the posts with a kernel at every spacing: cubic, the default, gives the "
        "same heights as `sample_raster`.")
        .def(nb::init<ResampleMethod>(), "method"_a = ResampleMethod::Cubic);
}

void BindChainSeams(nb::module_& m) {
    BindEnum<ChainSeamMode>(m, "ChainSeamMode",
                            "How a source chain joins a source to the gaps of those before it.");
    BindOptions<ChainSeams>(
        m, "ChainSeams",
        "How a source chain matches each source to the gaps of those before it: `mode`, the "
        "feather band's `width` and the `decay_length` over which height and slope fade out "
        "beyond an open edge, both in the sources' units (meters on a plane, degrees on a body).",
        {&ValidateChainSeams, "validate_chain_seams", "seams"})
        .def(nb::self == nb::self);
    m.def("seam_reach", &SeamReach, "seams"_a,
          "The distance from a gap's edge, in the sources' units, over which `seams` change the "
          "later source.");
    m.def("seam_lattice", &SeamLattice, "grid"_a, "seams"_a,
          "The lattice a seam at the edge of a source covering `grid` is solved on: its posts, "
          "widened by the seam's reach and 3 more posts.");
    m.attr("SEAM_REACH_DECAYS") = kSeamReachDecays;
}

void BindDiamondSquare(nb::module_& m) {
    BindEnum<DiamondSquareInterp>(m, "DiamondSquareInterp",
                                  "How diamond-square fills new nodes and reads between them.");
    BindOptions<DiamondSquareOptions>(
        m, "DiamondSquareOptions",
        "The fractal diamond-square refines posts with: `fractal_dimension` and `scale` (meters; "
        "None estimates them from the posts), `fit_window_px` (side in posts of the overlapping "
        "windows a None scale is fitted over, so each region follows its own roughness; 0 fits "
        "one scale to the posts), `interp`, `min_pixel_size` (posts' units, 0 for no limit) and "
        "`seed`.",
        {&ValidateDiamondSquareOptions, "validate_diamond_square_options", "options"})
        .def(nb::self == nb::self);
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
        .def_prop_ro("options", &DiamondSquareUpsampler::Options)
        .def("levels", &DiamondSquareUpsampler::Levels, "post_spacing"_a, "spacing"_a,
             "The levels refined at a sample spacing over posts `post_spacing` apart.");
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
    nb::class_<ShapeModelSource, Source>(
        m, "ShapeModelSource",
        "A shape-model body's own labels refined by `upsampler` (cubic when None): heights are "
        "the refined radius less the body's, which interpolates the labels bilinearly, so 0 at "
        "every label. Each ICQ face is a lattice of posts in degrees, read across its edges from "
        "its neighbors and blended within half a label of them, so there are no seams.")
        .def(
            "__init__",
            [](ShapeModelSource* self, const Surface& body, std::shared_ptr<Upsampler> upsampler) {
                new (self) ShapeModelSource(body, std::move(upsampler));
            },
            "body"_a, "upsampler"_a = nb::none())
        .def_prop_ro("body", &ShapeModelSource::Body)
        .def_prop_ro(
            "upsamplers",
            [](const ShapeModelSource& s) {
                std::vector<std::shared_ptr<Upsampler>> upsamplers;
                for (const auto& upsampler : s.UpsamplersInUse()) {
                    upsamplers.push_back(Mutable(upsampler));
                }
                return upsamplers;
            },
            "The upsampler of each of the six faces, fitted to its posts.");
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
    BindOptions<RasterFieldOptions>(
        m, "RasterFieldOptions",
        "How a raster field is read: `method` (nearest or bilinear), `scale` multiplying every "
        "cell, the `fallback` where the raster has no value (None for none), and whether x is a "
        "`periodic_longitude`.",
        {&ValidateRasterFieldOptions, "validate_raster_field_options", "options"});
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
    BindChainSeams(m);
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
