#include "bindings.h"
#include "objects.h"
#include "options.h"
#include "tables.h"

#include "terraingen/terraingen.h"

#include <nanobind/operators.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>
#include <nanobind/trampoline.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace nb = nanobind;
using namespace nb::literals;

namespace terraingen::python {

namespace {

/// Calls the Python override `name` of `trampoline` with `args` if the type defines one.
template <typename Result, typename... Args>
std::optional<Result> CallOverride(const nb::detail::trampoline& trampoline, const char* name,
                                   std::uint64_t hash, Args&&... args) {
    const nb::detail::ticket ticket(trampoline, name, hash, false);
    if (!ticket.key.is_valid()) {
        return std::nullopt;
    }
    return nb::cast<Result>(trampoline.base().attr(ticket.key)(std::forward<Args>(args)...));
}

/// Calls the Python override `name` of `trampoline` with `raster` (passed by reference) and
/// `args`, if the type defines one; whether it did. The raster is wrapped only once the ticket
/// holds the GIL.
template <typename... Args>
bool CallRasterOverride(const nb::detail::trampoline& trampoline, const char* name,
                        std::uint64_t hash, Raster<float>& raster, Args&&... args) {
    const nb::detail::ticket ticket(trampoline, name, hash, false);
    if (!ticket.key.is_valid()) {
        return false;
    }
    trampoline.base().attr(ticket.key)(nb::cast(&raster, nb::rv_policy::reference),
                                       std::forward<Args>(args)...);
    return true;
}

/// Whether the Python type behind `trampoline` overrides `name`.
bool Overrides(const nb::detail::trampoline& trampoline, const char* name, std::uint64_t hash) {
    const nb::detail::ticket ticket(trampoline, name, hash, false);
    return ticket.key.is_valid();
}

/// A source written in Python: `grid()` and `height_at(x, y)` are required; `fill(heights)` fills
/// a whole raster at once, and `reference_point()` gives (x, y) noise is measured from.
class PySource : public Source {
  public:
    NB_TRAMPOLINE(Source);

    RasterGrid Grid() const override {
        NB_OVERRIDE_PURE_NAME("grid", Grid);
    }

    Vec2d ReferencePoint() const override {
        constexpr std::uint64_t hash = nb::detail::str_hash("reference_point");
        if (const auto point =
                CallOverride<std::pair<double, double>>(nb_trampoline, "reference_point", hash)) {
            return {point->first, point->second};
        }
        return {};
    }

    void Fill(Raster<float>& heights) const override {
        constexpr std::uint64_t hash = nb::detail::str_hash("fill");
        if (CallRasterOverride(nb_trampoline, "fill", hash, heights)) {
            return;
        }
        for (std::size_t row = 0; row < heights.Height(); ++row) {
            for (std::size_t col = 0; col < heights.Width(); ++col) {
                const Vec2d centre = heights.CellCenter(col, row);
                heights(col, row) = HeightAt(centre.x, centre.y);
            }
        }
    }

    float HeightAt(double x, double y) const override {
        NB_OVERRIDE_PURE_NAME("height_at", HeightAt, x, y);
    }
};

/// A layer written in Python: `apply_at(height, x, y, context)` changes one height, `apply(heights,
/// context)` a whole raster at once (faster with NumPy); either is enough. `max_feature_diameter()`
/// says how far, in metres, the layer's features reach, so tiles get their neighbours' features.
class PyLayer : public Layer {
  public:
    NB_TRAMPOLINE(Layer);

    void Apply(Raster<float>& heights, const LayerContext& context,
               TerrainFeatures& /*features*/) const override {
        constexpr std::uint64_t hash = nb::detail::str_hash("apply");
        if (CallRasterOverride(nb_trampoline, "apply", hash, heights, context)) {
            return;
        }
        for (std::size_t row = 0; row < heights.Height(); ++row) {
            for (std::size_t col = 0; col < heights.Width(); ++col) {
                const Vec2d centre = heights.CellCenter(col, row);
                heights(col, row) = ApplyAt(heights(col, row), centre.x, centre.y, context);
            }
        }
    }

    float ApplyAt(float height, double x, double y, const LayerContext& context) const override {
        constexpr std::uint64_t hash = nb::detail::str_hash("apply_at");
        if (const auto value =
                CallOverride<float>(nb_trampoline, "apply_at", hash, height, x, y, context)) {
            return *value;
        }
        if (!Overrides(nb_trampoline, "apply", nb::detail::str_hash("apply"))) {
            throw std::logic_error("A Python Layer must define apply_at or apply");
        }
        return Layer::ApplyAt(height, x, y, context);
    }

    double MaxFeatureDiameter() const override {
        NB_OVERRIDE_NAME("max_feature_diameter", MaxFeatureDiameter);
    }
};

/// A filter written in Python: `apply(heights, spacing)` changes the heights after every layer;
/// `margin(max_feature_diameter, spacing)` says how far, in metres, it reads around each cell (0
/// by default).
class PyFilter : public Filter {
  public:
    NB_TRAMPOLINE(Filter);

    void Apply(Raster<float>& heights, double spacing,
               const TerrainFeatures& /*features*/) const override {
        constexpr std::uint64_t hash = nb::detail::str_hash("apply");
        if (!CallRasterOverride(nb_trampoline, "apply", hash, heights, spacing)) {
            throw std::logic_error("A Python Filter must define apply");
        }
    }

    double Margin(double maxFeatureDiameter, double spacing) const override {
        constexpr std::uint64_t hash = nb::detail::str_hash("margin");
        return CallOverride<double>(nb_trampoline, "margin", hash, maxFeatureDiameter, spacing)
            .value_or(0.0);
    }
};

Vec2d ToVec(const std::pair<double, double>& p) {
    return {p.first, p.second};
}

CraterList ToCraterList(const std::vector<Crater>& craters) {
    CraterList list;
    for (Crater crater : craters) {
        crater.manual = true;
        list.Append(crater);
    }
    return list;
}

std::vector<Crater> FromCraterList(const CraterList& list) {
    std::vector<Crater> craters;
    for (std::size_t i = 0; i < list.Size(); ++i) {
        craters.push_back(list.At(i));
    }
    return craters;
}

void BindSurface(nb::module_& m) {
    nb::class_<Surface>(m, "Surface",
                        "What a terrain lies on: a plane, with coordinates in metres, or a sphere, "
                        "with coordinates in degrees of longitude and latitude and lengths in "
                        "metres.")
        .def(nb::init<>(), "A plane.")
        .def_static("plane", &Surface::Plane)
        .def_static("sphere", &Surface::Sphere, "radius"_a, "A sphere of `radius` metres.")
        .def_prop_ro("is_sphere", &Surface::IsSphere)
        .def_prop_ro("radius", &Surface::Radius)
        .def_prop_ro("unit_length", &Surface::UnitLength,
                     "Metres per coordinate unit: 1 on a plane, metres per degree along a "
                     "meridian on a sphere.")
        .def(
            "distance",
            [](const Surface& surface, std::pair<double, double> a, std::pair<double, double> b) {
                return surface.Distance({a.first, a.second}, {b.first, b.second});
            },
            "a"_a, "b"_a, "The distance in metres between two points, great-circle on a sphere.")
        .def(
            "position",
            [](const Surface& surface, std::pair<double, double> point) {
                const Vec3d p = surface.Position({point.first, point.second});
                return std::tuple{p.x, p.y, p.z};
            },
            "point"_a, "The 3D position (m) of a point; on a sphere, body-fixed with z north.")
        .def(
            "offset",
            [](const Surface& surface, std::pair<double, double> centre,
               std::pair<double, double> point) {
                const Vec2d o =
                    surface.Offset({centre.first, centre.second}, {point.first, point.second});
                return std::pair{o.x, o.y};
            },
            "centre"_a, "point"_a,
            "`point` as (east, north) metres from `centre` (azimuthal equidistant on a sphere).")
        .def(
            "locate",
            [](const Surface& surface, std::pair<double, double> centre,
               std::pair<double, double> offset) {
                const Vec2d p =
                    surface.Locate({centre.first, centre.second}, {offset.first, offset.second});
                return std::pair{p.x, p.y};
            },
            "centre"_a, "offset"_a, "The point (east, north) metres from `centre`.")
        .def(
            "frame",
            [](const Surface& surface, std::pair<double, double> point) {
                const LocalFrame frame = surface.Frame({point.first, point.second});
                const auto v = [](const Vec3d& d) { return std::tuple{d.x, d.y, d.z}; };
                return std::tuple{v(frame.east), v(frame.north), v(frame.up)};
            },
            "point"_a, "The (east, north, up) unit vectors at a point.")
        .def(
            "point_of",
            [](const Surface& surface, std::tuple<double, double, double> direction) {
                const Vec2d p = surface.PointOf(
                    {std::get<0>(direction), std::get<1>(direction), std::get<2>(direction)});
                return std::pair{p.x, p.y};
            },
            "direction"_a, "The surface point of a direction from the sphere's centre.")
        .def(
            "expand",
            [](const Surface& surface, std::tuple<double, double, double, double> region,
               double metres) {
                const Bounds2d b = surface.Expand(BoundsOf(region), metres);
                return std::tuple{b.minX, b.minY, b.maxX, b.maxY};
            },
            "region"_a, "metres"_a, "A box holding every point within `metres` of `region`.")
        .def(
            "contains",
            [](const Surface& surface, std::tuple<double, double, double, double> region,
               std::pair<double, double> point) {
                return surface.Contains(BoundsOf(region), {point.first, point.second});
            },
            "region"_a, "point"_a,
            "Whether `region` holds `point`, across the antimeridian and at the poles on a "
            "sphere.")
        .def("__eq__", [](const Surface& a, const Surface& b) { return a == b; });
    nb::enum_<Body>(m, "Body", "Bodies with a radius preset.")
        .value("MOON", Body::Moon)
        .value("MARS", Body::Mars)
        .value("MERCURY", Body::Mercury);
    m.def("mean_radius", &MeanRadius, "body"_a,
          "The body's volumetric mean radius in metres (NASA planetary fact sheets).");
    m.def("body_sphere", &BodySphere, "body"_a, "The sphere of the body's mean radius.");
}

void BindGridAndContext(nb::module_& m) {
    nb::class_<RasterGrid>(m, "RasterGrid",
                           "A `width` x `height` grid of cells placed by a "
                           "geotransform.")
        .def(
            "__init__",
            [](RasterGrid* self, std::size_t width, std::size_t height, GeoTransform transform) {
                new (self) RasterGrid{width, height, transform};
            },
            "width"_a, "height"_a, "transform"_a)
        .def_static(
            "centred",
            [](std::size_t width, std::size_t height, double pixelSize,
               std::pair<double, double> centre) {
                const double halfWidth = 0.5 * static_cast<double>(width) * pixelSize;
                const double halfHeight = 0.5 * static_cast<double>(height) * pixelSize;
                return RasterGrid{width, height,
                                  GeoTransform::NorthUp(centre.first - halfWidth,
                                                        centre.second + halfHeight, pixelSize)};
            },
            "width"_a, "height"_a, "pixel_size"_a, "centre"_a = std::pair{0.0, 0.0},
            "A north-up grid of square cells centred on `centre`.")
        .def_static("of", &RasterGrid::Of, "raster"_a, "The grid of `raster`.")
        .def_rw("width", &RasterGrid::width)
        .def_rw("height", &RasterGrid::height)
        .def_rw("transform", &RasterGrid::transform)
        .def_prop_ro("bounds",
                     [](const RasterGrid& grid) {
                         const Bounds2d b = grid.Bounds();
                         return std::tuple{b.minX, b.minY, b.maxX, b.maxY};
                     })
        .def_prop_ro("area", &RasterGrid::Area, "Area in square world units.")
        .def_prop_ro("pixel_size", &RasterGrid::PixelSize, "Cell size in world units.")
        .def("surface_area", &RasterGrid::SurfaceArea, "surface"_a,
             "The area the grid covers on `surface`, in square metres.")
        .def("spacing", &RasterGrid::Spacing, "surface"_a,
             "The sample spacing in metres on `surface`.")
        .def("make_raster", &RasterGrid::MakeRaster, "fill"_a = 0.0f,
             "A raster on this grid filled with `fill`.");

    nb::class_<LayerContext>(m, "LayerContext",
                             "How a layer is evaluated: the sample spacing (m) and threads.")
        .def(
            "__init__",
            [](LayerContext* self, double spacing, unsigned threads) {
                new (self) LayerContext{spacing, threads, nullptr};
            },
            "spacing"_a = 0.0, "threads"_a = 1)
        .def_ro("spacing", &LayerContext::spacing)
        .def_ro("threads", &LayerContext::threads);
}

void BindSources(nb::module_& m) {
    nb::class_<Source, PySource>(
        m, "Source",
        "Where a terrain's heights come from, over its grid. Subclass it and implement `grid()` "
        "(a RasterGrid) and `height_at(x, y)`, and optionally `fill(heights)` and "
        "`reference_point()`, for a source of your own.")
        .def(nb::init<>())
        .def("grid", &Source::Grid)
        .def("height_at", &Source::HeightAt, "x"_a, "y"_a)
        .def("reference_point",
             [](const Source& source) {
                 const Vec2d p = source.ReferencePoint();
                 return std::pair{p.x, p.y};
             })
        .def("fill", &Source::Fill, "heights"_a, "Sets every cell of `heights`.")
        .def_prop_ro("width", [](const Source& source) { return source.Grid().width; })
        .def_prop_ro("height", [](const Source& source) { return source.Grid().height; })
        .def_prop_ro("transform", [](const Source& source) { return source.Grid().transform; });

    nb::class_<FlatSource, Source>(m, "FlatSource",
                                   "A level datum at `elevation` over a `width` x `height` grid of "
                                   "`pixel_size` metres centred on `centre`.")
        .def(
            "__init__",
            [](FlatSource* self, std::size_t width, std::size_t height, double pixelSize,
               float elevation, std::pair<double, double> centre, const Surface& surface) {
                new (self) FlatSource(width, height, pixelSize / surface.UnitLength(), elevation,
                                      ToVec(centre));
            },
            "width"_a, "height"_a, "pixel_size"_a, "elevation"_a = 0.0f,
            "centre"_a = std::pair{0.0, 0.0}, "surface"_a = Surface{},
            "On a sphere `centre` is (longitude, latitude) in degrees and `pixel_size` stays in "
            "metres, converted to degrees along a meridian.");

    nb::enum_<ResampleMethod>(m, "ResampleMethod", "How a raster is interpolated.")
        .value("NEAREST", ResampleMethod::Nearest)
        .value("BILINEAR", ResampleMethod::Bilinear)
        .value("CUBIC", ResampleMethod::Cubic);
    nb::class_<GeoTiffSource, Source>(
        m, "GeoTiffSource",
        "Heights from band `band` of a GeoTIFF, optionally resampled to `pixel_size` metres and "
        "scaled (height * scale + offset).")
        .def(
            "__init__",
            [](GeoTiffSource* self, const std::filesystem::path& path, std::size_t band,
               std::optional<double> pixelSize, ResampleMethod resampling,
               std::optional<double> scale, std::optional<double> offset) {
                new (self) GeoTiffSource(path, band, pixelSize, resampling, scale, offset);
            },
            "path"_a, "band"_a = 0, "pixel_size"_a = nb::none(),
            "resampling"_a = ResampleMethod::Cubic, "scale"_a = nb::none(), "offset"_a = nb::none(),
            "`pixel_size` is in the file's units (degrees for a geographic file).");

    nb::class_<SourceChain, Source>(
        m, "SourceChain",
        "Several sources tried in order, each filling where those before it have no data; the "
        "grid is the first's.")
        .def(
            "__init__",
            [](SourceChain* self, const std::vector<std::shared_ptr<Source>>& sources) {
                new (self) SourceChain({sources.begin(), sources.end()});
            },
            "sources"_a);
}

void BindLayerBase(nb::module_& m) {
    nb::class_<Layer, PyLayer>(
        m, "Layer",
        "Relief added to a terrain. Subclass it and implement `apply_at(height, x, y, context)` "
        "or `apply(heights, context)` (a Raster changed in place), and optionally "
        "`max_feature_diameter()`, for a layer of your own.")
        .def(nb::init<>())
        .def(
            "apply",
            [](const Layer& layer, Raster<float>& heights, const LayerContext& context) {
                TerrainFeatures features;
                layer.Apply(heights, context, features);
            },
            "heights"_a, "context"_a)
        .def("apply_at", &Layer::ApplyAt, "height"_a, "x"_a, "y"_a, "context"_a)
        .def("max_feature_diameter", &Layer::MaxFeatureDiameter);

    nb::class_<NoiseRelief, Layer>(m, "NoiseRelief",
                                   "A stack of 1 to 8 noise layers added to the terrain, measured "
                                   "from `origin`.")
        .def(
            "__init__",
            [](NoiseRelief* self, std::vector<NoiseLayer> layers, std::pair<double, double> origin,
               const Surface& surface) {
                new (self)
                    NoiseRelief(SyntheticTerrainParams{std::move(layers)}, ToVec(origin), surface);
            },
            "layers"_a, "origin"_a = std::pair{0.0, 0.0}, "surface"_a = Surface{},
            "On a sphere the noise is 3D at each point's position, seamless everywhere, and "
            "`origin` is unused.");

    nb::class_<ScaleLayer, Layer>(m, "ScaleLayer", "height * scale + offset.")
        .def(nb::init<double, double>(), "scale"_a, "offset"_a = 0.0);
    nb::class_<ClampLayer, Layer>(m, "ClampLayer", "Heights held between `min` and `max`.")
        .def(nb::init<std::optional<double>, std::optional<double>>(), "min"_a = nb::none(),
             "max"_a = nb::none());

    nb::enum_<BlendShape>(m, "BlendShape", "How a weight falls from 1 to 0.")
        .value("LINEAR", BlendShape::Linear)
        .value("SMOOTHSTEP", BlendShape::Smoothstep)
        .value("SMOOTHERSTEP", BlendShape::Smootherstep)
        .value("GAUSSIAN", BlendShape::Gaussian);
    auto fade = nb::class_<ResolutionFade>(
        m, "ResolutionFade",
        "How features too small to resolve fade out, by how many samples span them: absent up "
        "to `start_samples`, whole from `end_samples`.");
    BindKwargsInit<ResolutionFade>(fade);
    fade.def_rw("start_samples", &ResolutionFade::startSamples)
        .def_rw("end_samples", &ResolutionFade::endSamples)
        .def_rw("shape", &ResolutionFade::shape);

    nb::class_<RegionShape>(m, "RegionShape", "A rectangle or circle in surface coordinates.")
        .def_static(
            "rectangle",
            [](double minX, double minY, double maxX, double maxY) {
                return RegionShape::Rectangle({minX, minY, maxX, maxY});
            },
            "min_x"_a, "min_y"_a, "max_x"_a, "max_y"_a)
        .def_static(
            "circle",
            [](double x, double y, double radius) { return RegionShape::Circle({x, y}, radius); },
            "x"_a, "y"_a, "radius"_a)
        .def("distance_outside", [](const RegionShape& shape, double x, double y) {
            return shape.DistanceOutside(x, y);
        });
    nb::class_<RegionLayer, Layer>(
        m, "RegionLayer",
        "`layer` applied only inside `shape`, faded out over `feather` metres outside it.")
        .def(
            "__init__",
            [](RegionLayer* self, std::shared_ptr<Layer> layer, RegionShape shape, double feather,
               BlendShape blend, const Surface& surface) {
                new (self) RegionLayer(std::move(layer), shape, feather, blend, surface);
            },
            "layer"_a, "shape"_a, "feather"_a = 0.0, "blend"_a = BlendShape::Smoothstep,
            "surface"_a = Surface{})
        .def("weight", &RegionLayer::Weight, "x"_a, "y"_a)
        .def_prop_ro("inner", &RegionLayer::Inner, nb::rv_policy::reference_internal,
                     "The layer applied inside the region.");

    auto roughness = nb::class_<RoughnessOptions>(
        m, "RoughnessOptions",
        "Fine roughness of `rms_slope` (m/m) over `baseline` metres with Hurst exponent `hurst`, "
        "between `min_wavelength` and `max_wavelength` metres.");
    BindKwargsInit<RoughnessOptions>(roughness);
    roughness.def_rw("rms_slope", &RoughnessOptions::rmsSlope)
        .def_rw("baseline", &RoughnessOptions::baseline)
        .def_rw("hurst", &RoughnessOptions::hurst)
        .def_rw("max_wavelength", &RoughnessOptions::maxWavelength)
        .def_rw("min_wavelength", &RoughnessOptions::minWavelength)
        .def_rw("seed", &RoughnessOptions::seed)
        .def_rw("fade", &RoughnessOptions::fade);
    nb::class_<RoughnessLayer, Layer>(m, "RoughnessLayer", "Fine roughness of a given RMS slope.")
        .def(nb::init<RoughnessOptions, Surface>(), "options"_a = RoughnessOptions{},
             "surface"_a = Surface{})
        .def("rms_slope", &RoughnessLayer::RmsSlope, "baseline"_a)
        .def("wavelengths", &RoughnessLayer::Wavelengths,
             "The wavelengths (m) of the roughness octaves.");

    auto exposure = nb::class_<ExposureParams>(
        m, "ExposureParams",
        "How exposed the ground is (crests and steep slopes shed regolith): rises and slopes "
        "for soil and boulder, read over `reach` metres.");
    BindKwargsInit<ExposureParams>(exposure);
    exposure.def_rw("reach", &ExposureParams::reach)
        .def_rw("rise_soil", &ExposureParams::riseSoil)
        .def_rw("rise_boulder", &ExposureParams::riseBoulder)
        .def_rw("slope_soil", &ExposureParams::slopeSoil)
        .def_rw("slope_boulder", &ExposureParams::slopeBoulder)
        .def_rw("floor_cover", &ExposureParams::floorCover)
        .def_rw("floor_patch", &ExposureParams::floorPatch)
        .def_rw("seed", &ExposureParams::seed);

    auto bedforms = nb::class_<BedformOptions>(
        m, "BedformOptions",
        "Ripples and dunes: `wavelength` and `height` (m), crests across `wind_azimuth` (degrees "
        "from north), covering `cover` of the ground in fields `field_size` metres across.");
    BindKwargsInit<BedformOptions>(bedforms);
    bedforms.def_rw("wavelength", &BedformOptions::wavelength)
        .def_rw("height", &BedformOptions::height)
        .def_rw("wind_azimuth", &BedformOptions::windAzimuth)
        .def_rw("azimuth_spread", &BedformOptions::azimuthSpread)
        .def_rw("lee_fraction", &BedformOptions::leeFraction)
        .def_rw("sinuosity", &BedformOptions::sinuosity)
        .def_rw("cover", &BedformOptions::cover)
        .def_rw("field_size", &BedformOptions::fieldSize)
        .def_rw("seed", &BedformOptions::seed)
        .def_rw("exposure", &BedformOptions::exposure)
        .def_rw("exposure_spacing", &BedformOptions::exposureSpacing)
        .def_rw("fade", &BedformOptions::fade);
    m.def("mars_large_ripples", &MarsLargeRipples, "Martian large ripples.");
    m.def("mars_transverse_aeolian_ridges", &MarsTransverseAeolianRidges,
          "Martian transverse aeolian ridges.");
    nb::class_<BedformLayer, Layer>(m, "BedformLayer", "Aeolian bedforms.")
        .def(nb::init<BedformOptions, Surface>(), "options"_a, "surface"_a = Surface{})
        .def(
            "cover_at",
            [](const BedformLayer& layer, double x, double y, const PyTerrain* terrain,
               std::size_t layers) {
                if (terrain == nullptr) {
                    return layer.CoverAt(x, y);
                }
                const Terrain built = terrain->Get();
                const TerrainGround ground(built, layers);
                return layer.CoverAt(x, y, &ground);
            },
            "x"_a, "y"_a, "terrain"_a = nb::none(), "layers"_a = 0,
            "The share of (x, y) covered by bedforms; bedforms that read the exposure field need "
            "the ground beneath them: `terrain`'s source and its first `layers` layers.");
}

void BindCraterLayers(nb::module_& m) {
    nb::class_<Crater>(m, "Crater",
                       "One crater: centre (m), rim-to-rim diameter (m), age (Gyr) and a stable "
                       "ID (0 for one hashed from its position and size).")
        .def(
            "__init__",
            [](Crater* self, double x, double y, double diameter, double ageGyr, std::uint64_t id) {
                new (self) Crater{x, y, diameter, ageGyr, id, true};
            },
            "x"_a, "y"_a, "diameter"_a, "age_gyr"_a = 0.0, "id"_a = 0)
        .def_rw("x", &Crater::x)
        .def_rw("y", &Crater::y)
        .def_rw("diameter", &Crater::diameter)
        .def_rw("age_gyr", &Crater::ageGyr)
        .def_rw("id", &Crater::id)
        .def_rw("manual", &Crater::manual)
        .def("__repr__", [](const Crater& c) {
            return "Crater(" + std::to_string(c.x) + ", " + std::to_string(c.y) + ", " +
                   std::to_string(c.diameter) + ", age_gyr=" + std::to_string(c.ageGyr) + ")";
        });

    nb::class_<CraterAttributes>(m, "CraterAttributes", "A crater's shape as applied.")
        .def_ro("floor_depth", &CraterAttributes::floorDepth)
        .def_ro("rim_height", &CraterAttributes::rimHeight)
        .def_ro("depth_ratio", &CraterAttributes::depthRatio)
        .def_ro("freshness", &CraterAttributes::freshness)
        .def_ro("complex", &CraterAttributes::complex);

    auto spacingClass = nb::class_<CraterSpacing>(
        m, "CraterSpacing",
        "Keeps craters of similar size apart: two conflict when their diameters differ by at most "
        "`diameter_ratio` (at least 1) and their centres are closer than `separation` times the "
        "larger diameter. A separation of 0 disables it.");
    BindKwargsInit<CraterSpacing>(spacingClass);
    spacingClass.def_rw("diameter_ratio", &CraterSpacing::diameterRatio)
        .def_rw("separation", &CraterSpacing::separation);

    auto generation = nb::class_<CraterGenerationOptions>(
        m, "CraterGenerationOptions",
        "Which craters are generated: diameters from `min_diameter` to `max_diameter` (m), kept "
        "apart by `spacing`, at most `max_craters` (0 for no cap), plus `manual` ones.");
    BindKwargsInit<CraterGenerationOptions>(generation);
    generation.def_rw("min_diameter", &CraterGenerationOptions::dMin)
        .def_rw("max_diameter", &CraterGenerationOptions::dMax)
        .def_rw("spacing", &CraterGenerationOptions::spacing)
        .def_rw("max_craters", &CraterGenerationOptions::maxCraters)
        .def_rw("cell_size_factor", &CraterGenerationOptions::cellSizeFactor)
        .def_rw("max_expected_craters", &CraterGenerationOptions::maxExpectedCraters)
        .def_prop_rw(
            "manual",
            [](const CraterGenerationOptions& options) { return FromCraterList(options.manual); },
            [](CraterGenerationOptions& options, const std::vector<Crater>& craters) {
                options.manual = ToCraterList(craters);
            },
            "Hand-placed craters, kept whatever the density.");

    nb::enum_<CraterOrder>(m, "CraterOrder", "The order overlapping craters are applied in.")
        .value("OLDEST_FIRST", CraterOrder::OldestFirst)
        .value("LARGEST_FIRST", CraterOrder::LargestFirst);
    nb::enum_<CraterDatum>(m, "CraterDatum", "The surface a crater's interior is cut relative to.")
        .value("NONE", CraterDatum::None)
        .value("PLANE", CraterDatum::Plane)
        .value("MEAN_HEIGHT", CraterDatum::MeanHeight)
        .value("CENTRE_HEIGHT", CraterDatum::CentreHeight);
    auto blend = nb::class_<CraterBlend>(
        m, "CraterBlend",
        "Where a crater's replaced interior blends into the draped exterior, in radii.");
    BindKwargsInit<CraterBlend>(blend);
    blend.def_rw("start_radii", &CraterBlend::startRadii)
        .def_rw("end_radii", &CraterBlend::endRadii)
        .def_rw("shape", &CraterBlend::shape)
        .def("weight", &CraterBlend::Weight, "radii"_a,
             "The interior weight `radii` crater radii from the centre.");

    nb::enum_<ValueDistribution::Kind>(m, "ValueDistributionKind")
        .value("CONSTANT", ValueDistribution::Kind::Constant)
        .value("UNIFORM", ValueDistribution::Kind::Uniform)
        .value("NORMAL", ValueDistribution::Kind::Normal)
        .value("LOG_NORMAL", ValueDistribution::Kind::LogNormal);
    nb::class_<ValueDistribution>(
        m, "ValueDistribution",
        "A random value: constant `a`, uniform from `a` to `b`, normal or log-normal with mean "
        "`a` and deviation `b`, clipped to [min, max].")
        .def(
            "__init__",
            [](ValueDistribution* self, ValueDistribution::Kind kind, double a, double b,
               std::optional<double> min, std::optional<double> max) {
                new (self) ValueDistribution{
                    kind, a, b, min.value_or(-std::numeric_limits<double>::infinity()), Bound(max)};
            },
            "kind"_a = ValueDistribution::Kind::Constant, "a"_a = 0.0, "b"_a = 0.0,
            "min"_a = nb::none(), "max"_a = nb::none())
        .def_rw("kind", &ValueDistribution::kind)
        .def_rw("a", &ValueDistribution::a)
        .def_rw("b", &ValueDistribution::b)
        .def_rw("min", &ValueDistribution::min)
        .def_rw("max", &ValueDistribution::max);
    nb::enum_<RimRoughnessModel>(m, "RimRoughnessModel", "How irregular crater rims are drawn.")
        .value("POWER_LAW", RimRoughnessModel::PowerLaw)
        .value("DU_2024", RimRoughnessModel::Du2024);
    auto selection = nb::class_<CraterSelection>(
        m, "CraterSelection",
        "Which craters a model applies to: diameters (m) and ages (Gyr) within ranges, and manual "
        "craters only with `manual`.");
    BindKwargsInit<CraterSelection>(selection);
    selection.def_rw("min_diameter", &CraterSelection::minDiameter)
        .def_rw("max_diameter", &CraterSelection::maxDiameter)
        .def_rw("min_age_gyr", &CraterSelection::minAgeGyr)
        .def_rw("max_age_gyr", &CraterSelection::maxAgeGyr)
        .def_rw("manual", &CraterSelection::manual)
        .def("includes", &CraterSelection::Includes, "crater"_a, "Whether `crater` is selected.");
    m.def("validate_crater_selection", &ValidateCraterSelection, "selection"_a,
          "what"_a = "Crater selection", "Raises ValueError naming the first invalid setting.");

    auto irregularity = nb::class_<CraterIrregularity>(
        m, "CraterIrregularity",
        "Irregular, elongated crater outlines for the craters it selects: rims roughened by "
        "random harmonics and some craters stretched into ellipses.");
    BindKwargsInit<CraterIrregularity>(irregularity);
    irregularity.def_rw("enabled", &CraterIrregularity::enabled)
        .def_rw("seed", &CraterIrregularity::seed)
        .def_rw("selection", &CraterIrregularity::selection);
    BindSelection(irregularity, &CraterIrregularity::selection, "min_diameter", "max_diameter");
    irregularity.def_rw("rough_share", &CraterIrregularity::roughShare)
        .def_rw("model", &CraterIrregularity::model)
        .def_rw("scatter", &CraterIrregularity::scatter)
        .def_rw("max_deviation", &CraterIrregularity::maxDeviation)
        .def_rw("roughness", &CraterIrregularity::roughness)
        .def_rw("spectral_slope", &CraterIrregularity::spectralSlope)
        .def_rw("min_harmonic", &CraterIrregularity::minHarmonic)
        .def_rw("max_harmonic", &CraterIrregularity::maxHarmonic)
        .def_rw("elongated_share", &CraterIrregularity::elongatedShare)
        .def_rw("ellipticity", &CraterIrregularity::ellipticity)
        .def_rw("heading", &CraterIrregularity::heading)
        .def_rw("heading_spread", &CraterIrregularity::headingSpread);

    auto apply = nb::class_<CraterApplyOptions>(
        m, "CraterApplyOptions",
        "How craters are applied: the `datum` interiors are cut relative to (fit over "
        "`datum_fit_radii` radii; with none, craters are added to the terrain and the settings "
        "after it are unused), how much of the interior replaces the older relief "
        "(`interior_replacement`, 0 to 1), the `blend` into the exterior, the `order` overlapping "
        "craters are applied in, the resolution `fade` and rim `irregularity`.");
    BindKwargsInit<CraterApplyOptions>(apply);
    apply.def_rw("order", &CraterApplyOptions::order)
        .def_rw("fade", &CraterApplyOptions::fade)
        .def_rw("datum", &CraterApplyOptions::datum)
        .def_rw("datum_fit_radii", &CraterApplyOptions::datumFitRadii)
        .def_rw("interior_replacement", &CraterApplyOptions::interiorReplacement)
        .def_rw("blend", &CraterApplyOptions::blend)
        .def_rw("irregularity", &CraterApplyOptions::irregularity);

    auto layerOptionsClass = nb::class_<CraterLayerOptions>(
        m, "CraterLayerOptions",
        "A crater layer's settings: whether to `generate` craters from the density, its `seed`, "
        "the `generation` options and the `apply` options.");
    BindKwargsInit<CraterLayerOptions>(layerOptionsClass);
    layerOptionsClass.def_rw("generate", &CraterLayerOptions::generate)
        .def_rw("surface", &CraterLayerOptions::surface,
                "What the craters lie on; on a sphere they are placed by longitude and latitude.")
        .def_rw("seed", &CraterLayerOptions::seed)
        .def_rw("generation", &CraterLayerOptions::generation)
        .def_rw("apply", &CraterLayerOptions::apply);

    nb::class_<CraterLayer, Layer>(
        m, "CraterLayer",
        "Craters drawn from `models.density` and shaped by its profile and degradation, plus any "
        "manual craters, applied to the terrain.")
        .def(nb::init<CraterModels, CraterLayerOptions>(), "models"_a,
             "options"_a = CraterLayerOptions{})
        .def(
            "craters",
            [](const CraterLayer& layer, std::tuple<double, double, double, double> region,
               double spacing) {
                const CraterList craters = layer.Craters(BoundsOf(region), spacing);
                std::vector<CraterAttributes> attributes;
                for (std::size_t i = 0; i < craters.Size(); ++i) {
                    attributes.push_back(layer.Attributes(craters.At(i)));
                }
                return CraterTable(craters, attributes);
            },
            "region"_a, "spacing"_a = 0.0,
            "The craters over `region` (min_x, min_y, max_x, max_y) as NumPy columns, without "
            "evaluating any heights.")
        .def("attributes", &CraterLayer::Attributes, "crater"_a)
        .def_prop_ro("models", &CraterLayer::Models)
        .def_prop_ro("options", &CraterLayer::Options);

    auto secondary = nb::class_<SecondaryCraterOptions>(
        m, "SecondaryCraterOptions",
        "Secondary craters around a crater layer's primaries; `onset_count` (the number at "
        "least `count_fraction` of the primary's diameter across, per primary-sized area at the "
        "onset) must be given.");
    BindKwargsInit<SecondaryCraterOptions>(secondary);
    secondary.def_rw("seed", &SecondaryCraterOptions::seed)
        .def_rw("onset_count", &SecondaryCraterOptions::onsetCount)
        .def_rw("count_fraction", &SecondaryCraterOptions::countFraction)
        .def_rw("size_slope", &SecondaryCraterOptions::sizeSlope)
        .def_rw("min_diameter", &SecondaryCraterOptions::minDiameter)
        .def_rw("max_diameter", &SecondaryCraterOptions::maxDiameter)
        .def_rw("outer_range", &SecondaryCraterOptions::outerRange)
        .def_rw("selection", &SecondaryCraterOptions::selection);
    BindSelection(secondary, &SecondaryCraterOptions::selection, "min_primary_diameter",
                  "max_primary_diameter");
    nb::class_<SecondaryCraterLayer, Layer>(
        m, "SecondaryCraterLayer",
        "Secondary craters around the craters of `primaries` (Singer et al. 2020 sizes, "
        "Chapman 1968 fall-off), shaped by `profile` and `degradation`.")
        .def(
            "__init__",
            [](SecondaryCraterLayer* self, std::shared_ptr<CraterLayer> primaries,
               std::shared_ptr<CraterProfile> profile,
               std::shared_ptr<DegradationModel> degradation, SecondaryCraterOptions options) {
                new (self) SecondaryCraterLayer(std::move(primaries),
                                                {std::move(profile), std::move(degradation)},
                                                std::move(options));
            },
            "primaries"_a, "profile"_a, "degradation"_a, "options"_a)
        .def(
            "secondaries",
            [](const SecondaryCraterLayer& layer, std::tuple<double, double, double, double> region,
               double spacing) {
                return CraterTable(layer.Secondaries(BoundsOf(region), spacing));
            },
            "region"_a, "spacing"_a = 0.0)
        .def("outer_range", &SecondaryCraterLayer::OuterRange, "diameter"_a);
    m.def("singer_max_secondary_diameter", &SingerMaxSecondaryDiameter, "diameter"_a, "range"_a,
          "The largest secondary (m) at `range` metres from a primary `diameter` metres across "
          "(Singer et al. 2020).");
    m.def("secondary_onset_range", &SecondaryOnsetRange, "diameter"_a,
          "How far from its centre (m) a primary throws its first secondaries.");
    m.def("singer_range_for_size", &SingerRangeForSize, "diameter"_a, "size"_a,
          "The range (m) beyond which a primary `diameter` metres across throws no secondary "
          "`size` metres across (Singer et al. 2020); infinite up to 1 km.");
    m.def("elliott_median_ray_length", &ElliottMedianRayLength, "diameter"_a,
          "coefficient"_a = 6.59, "exponent"_a = 1.27,
          "The median ray length (m) of a crater `diameter` metres across (Elliott et al. 2018).");
}

void BindBoulderShapes(nb::module_& m) {
    auto range =
        nb::class_<ValueRange>(m, "ValueRange", "An interval values are drawn uniformly from.");
    range
        .def(
            "__init__",
            [](ValueRange* self, double min, double max) { new (self) ValueRange{min, max}; },
            "min"_a = 0.0, "max"_a = 0.0)
        .def_rw("min", &ValueRange::min)
        .def_rw("max", &ValueRange::max);
    nb::enum_<BaseSolid>(m, "BaseSolid", "The solid a boulder shape starts from.")
        .value("ICOSAHEDRON", BaseSolid::Icosahedron)
        .value("CUBE", BaseSolid::Cube)
        .value("PYRAMID", BaseSolid::Pyramid)
        .value("PRISM", BaseSolid::Prism)
        .value("TETRAHEDRON", BaseSolid::Tetrahedron);
    auto style = nb::class_<BoulderStyle>(
        m, "BoulderStyle",
        "One kind of boulder: its `solid`, stretched to axes 1 : `intermediate` : `short_axis`, "
        "rounded toward an ellipsoid by `rounding`, roughened by noise and cut by fracture planes; "
        "`weight` is its share of boulders.");
    BindKwargsInit<BoulderStyle>(style);
    style.def_rw("name", &BoulderStyle::name)
        .def_rw("weight", &BoulderStyle::weight)
        .def_rw("solid", &BoulderStyle::solid)
        .def_rw("intermediate", &BoulderStyle::intermediate)
        .def_rw("short_axis", &BoulderStyle::shortAxis)
        .def_rw("rounding", &BoulderStyle::rounding)
        .def_rw("min_fractures", &BoulderStyle::minFractures)
        .def_rw("max_fractures", &BoulderStyle::maxFractures)
        .def_rw("fracture_depth", &BoulderStyle::fractureDepth)
        .def_rw("max_fracture_tilt", &BoulderStyle::maxFractureTilt)
        .def_rw("noise_amplitude", &BoulderStyle::noiseAmplitude)
        .def_rw("noise_wavelength", &BoulderStyle::noiseWavelength)
        .def_rw("noise_octaves", &BoulderStyle::noiseOctaves);
    m.def("default_boulder_styles", &DefaultBoulderStyles,
          "The default mix: blocks and slabs, rounded boulders, wedges, pyramids and shards.");
    auto shapes = nb::class_<BoulderShapeOptions>(
        m, "BoulderShapeOptions",
        "Boulder shapes: the `styles` to draw from, `subdivisions` of each solid and a `seed`.");
    BindKwargsInit<BoulderShapeOptions>(shapes);
    shapes.def_rw("styles", &BoulderShapeOptions::styles)
        .def_rw("subdivisions", &BoulderShapeOptions::subdivisions)
        .def_rw("seed", &BoulderShapeOptions::seed);
    nb::class_<BoulderShape>(m, "BoulderShape",
                             "A boulder shape with a long axis of 1 along x, `intermediate` along "
                             "y and `short_axis` along z.")
        .def_ro("mesh", &BoulderShape::mesh)
        .def_ro("style", &BoulderShape::style)
        .def_ro("intermediate", &BoulderShape::intermediate)
        .def_ro("short_axis", &BoulderShape::shortAxis);
    m.def("generate_boulder_shape", &GenerateBoulderShape, "shape_seed"_a,
          "options"_a = BoulderShapeOptions{},
          "The shape `shape_seed` gives: the same seed always gives the same shape.");
    m.def("boulder_shape_seed", &BoulderShapeSeed, "id"_a, "prototypes"_a = 0,
          "The shape seed of boulder `id`, or of one of `prototypes` shared shapes.");
}

void BindBoulderLayers(nb::module_& m) {
    auto patches = nb::class_<BoulderPatches>(
        m, "BoulderPatches",
        "Patches of richer and poorer boulder cover, about `size` metres across: the abundance "
        "varies from 1 / `spread` to `spread` times its mean (1 for the same everywhere).");
    BindKwargsInit<BoulderPatches>(patches);
    patches.def_rw("spread", &BoulderPatches::spread).def_rw("size", &BoulderPatches::size);

    nb::enum_<EjectaProfile>(m, "EjectaProfile",
                             "How ejecta boulders fall off from a crater's rim.")
        .value("SMOOTHSTEP", EjectaProfile::Smoothstep)
        .value("EXPONENTIAL", EjectaProfile::Exponential);
    nb::enum_<EjectaAging>(m, "EjectaAging", "How ejecta boulders fade as their crater ages.")
        .value("FRESHNESS", EjectaAging::Freshness)
        .value("AGE", EjectaAging::Age);
    auto ejecta = nb::class_<BoulderEjecta>(
        m, "BoulderEjecta",
        "Boulders thrown out by craters at least `min_crater_diameter` across (or deep enough to "
        "reach bedrock under `regolith_thickness`): extra `coverage` near the rim, falling off "
        "to `reach` radii, fading as the crater ages.");
    BindKwargsInit<BoulderEjecta>(ejecta);
    ejecta.def_rw("min_crater_diameter", &BoulderEjecta::minCraterDiameter)
        .def_rw("regolith_thickness", &BoulderEjecta::regolithThickness)
        .def_rw("excavation_depth_ratio", &BoulderEjecta::excavationDepthRatio)
        .def_rw("distribution", &BoulderEjecta::distribution)
        .def_rw("coverage", &BoulderEjecta::coverage)
        .def_rw("profile", &BoulderEjecta::profile)
        .def_rw("reach", &BoulderEjecta::reach)
        .def_rw("decay_radii", &BoulderEjecta::decayRadii)
        .def_rw("floor_share", &BoulderEjecta::floorShare)
        .def_rw("aging", &BoulderEjecta::aging)
        .def_rw("min_freshness", &BoulderEjecta::minFreshness)
        .def_rw("lifetime_gyr", &BoulderEjecta::lifetimeGyr)
        .def_rw("aging_exponent", &BoulderEjecta::agingExponent);

    nb::enum_<BoulderRelief>(m, "BoulderRelief", "How boulders are drawn into the heights.")
        .value("DOME", BoulderRelief::Dome)
        .value("SHAPE", BoulderRelief::Shape)
        .value("NONE", BoulderRelief::None);
    auto models = nb::class_<BoulderModelThreshold>(
        m, "BoulderModelThreshold",
        "Which boulders are placed as 3D boulder models: at least `min_diameter` metres across, or "
        "`min_samples` samples across.");
    BindKwargsInit<BoulderModelThreshold>(models);
    models.def_rw("min_diameter", &BoulderModelThreshold::minDiameter)
        .def_rw("min_samples", &BoulderModelThreshold::minSamples);

    auto boulderOptionsClass = nb::class_<BoulderLayerOptions>(
        m, "BoulderLayerOptions",
        "A boulder layer's settings: sizes from Golombek and Rapp's model for `coverage` (or "
        "`distribution`), between `min_diameter` and `max_diameter`; `patches`, `ejecta` and "
        "`exposure` shape where they lie; heights, burial and lean shape each boulder; `relief` "
        "and "
        "`models` how they are drawn.");
    BindKwargsInit<BoulderLayerOptions>(boulderOptionsClass);
    using Options = BoulderLayerOptions;
    BindGrouped(boulderOptionsClass, "coverage", &Options::sizes, &BoulderSizes::coverage);
    BindGrouped(boulderOptionsClass, "qa", &Options::sizes, &BoulderSizes::qConstant);
    BindGrouped(boulderOptionsClass, "qb", &Options::sizes, &BoulderSizes::qCoverageCoefficient);
    BindGrouped(boulderOptionsClass, "distribution", &Options::sizes, &BoulderSizes::distribution);
    BindGrouped(boulderOptionsClass, "min_diameter", &Options::sizes, &BoulderSizes::minDiameter);
    BindGrouped(boulderOptionsClass, "max_diameter", &Options::sizes, &BoulderSizes::maxDiameter);
    boulderOptionsClass.def_rw("seed", &Options::seed)
        .def_rw("patches", &Options::patches)
        .def_rw("ejecta", &Options::ejecta);
    BindGrouped(boulderOptionsClass, "exposure", &Options::bedrock, &BoulderBedrock::exposure);
    BindGrouped(boulderOptionsClass, "bedrock_abundance", &Options::bedrock,
                &BoulderBedrock::abundance);
    BindGrouped(boulderOptionsClass, "exposure_spacing", &Options::bedrock,
                &BoulderBedrock::exposureSpacing);
    BindGrouped(boulderOptionsClass, "min_height_ratio", &Options::pose,
                &BoulderPose::minHeightRatio);
    BindGrouped(boulderOptionsClass, "max_height_ratio", &Options::pose,
                &BoulderPose::maxHeightRatio);
    BindGrouped(boulderOptionsClass, "min_burial", &Options::pose, &BoulderPose::minBurial);
    BindGrouped(boulderOptionsClass, "max_burial", &Options::pose, &BoulderPose::maxBurial);
    BindGrouped(boulderOptionsClass, "max_lean", &Options::pose, &BoulderPose::maxLean);
    BindGrouped(boulderOptionsClass, "relief", &Options::rendering, &BoulderRendering::relief);
    BindGrouped(boulderOptionsClass, "models", &Options::rendering, &BoulderRendering::models);
    BindGrouped(boulderOptionsClass, "shapes", &Options::rendering, &BoulderRendering::shapes);
    BindGrouped(boulderOptionsClass, "shape_prototypes", &Options::rendering,
                &BoulderRendering::shapePrototypes);
    boulderOptionsClass.def_rw("fade", &Options::fade);

    nb::class_<BoulderLayer, Layer>(m, "BoulderLayer",
                                    "Boulders drawn into the heights, the largest optionally "
                                    "placed as 3D boulder models; `craters` "
                                    "supplies the craters that throw ejecta boulders.")
        .def(
            "__init__",
            [](BoulderLayer* self, BoulderLayerOptions options,
               std::shared_ptr<CraterLayer> craters, const Surface& surface) {
                new (self) BoulderLayer(std::move(options), std::move(craters), surface);
            },
            "options"_a = BoulderLayerOptions{}, "craters"_a = nb::none(), "surface"_a = Surface{})
        .def(
            "boulders",
            [](const BoulderLayer& layer, std::tuple<double, double, double, double> region,
               double spacing) { return BoulderTable(layer.Boulders(BoundsOf(region), spacing)); },
            "region"_a, "spacing"_a = 0.0,
            "The boulders over `region` (min_x, min_y, max_x, max_y) as NumPy columns, without "
            "evaluating any heights or ground.")
        .def(
            "abundance_at",
            [](const BoulderLayer& layer, double x, double y, const PyTerrain* terrain,
               std::size_t layers) {
                BoulderAbundance abundance;
                if (terrain == nullptr) {
                    abundance = layer.AbundanceAt(x, y);
                } else {
                    const Terrain built = terrain->Get();
                    const TerrainGround ground(built, layers);
                    abundance = layer.AbundanceAt(x, y, &ground);
                }
                return std::pair{abundance.background, abundance.ejecta};
            },
            "x"_a, "y"_a, "terrain"_a = nb::none(), "layers"_a = 0,
            "The boulder abundance at (x, y): (background, ejecta). With the exposure field, give "
            "the ground beneath the boulders: `terrain`'s source and its first `layers` layers.")
        .def(
            "density_at",
            [](const BoulderLayer& layer, double diameter, double x, double y,
               const PyTerrain* terrain, std::size_t layers) {
                if (terrain == nullptr) {
                    return layer.DensityAt(diameter, x, y);
                }
                const Terrain built = terrain->Get();
                const TerrainGround ground(built, layers);
                return layer.DensityAt(diameter, x, y, &ground);
            },
            "diameter"_a, "x"_a, "y"_a, "terrain"_a = nb::none(), "layers"_a = 0,
            "Boulders at least `diameter` across per square metre at (x, y).")
        .def_prop_ro("options", &BoulderLayer::Options);
}

void BindFilters(nb::module_& m) {
    nb::class_<Filter, PyFilter>(
        m, "Filter",
        "A transform of a terrain's heights after its layers. Subclass it and implement "
        "`apply(heights, spacing)`, and `margin(max_feature_diameter, spacing)` if it reads "
        "neighbouring cells, for a filter of your own.")
        .def(nb::init<>())
        .def(
            "apply",
            [](const Filter& filter, Raster<float>& heights, double spacing) {
                filter.Apply(heights, spacing, TerrainFeatures{});
            },
            "heights"_a, "spacing"_a)
        .def("margin", &Filter::Margin, "max_feature_diameter"_a, "spacing"_a);
    auto smoothing = nb::class_<RimSmoothing>(
        m, "RimSmoothing",
        "Crater rim smoothing: a Gaussian of `sigma_radii` crater radii over the band from "
        "`inner_radii` to `outer_radii`, fading along `shape` away from the rim, at `strength`.");
    BindKwargsInit<RimSmoothing>(smoothing);
    smoothing.def_rw("sigma_radii", &RimSmoothing::sigmaRadii)
        .def_rw("inner_radii", &RimSmoothing::innerRadii)
        .def_rw("outer_radii", &RimSmoothing::outerRadii)
        .def_rw("shape", &RimSmoothing::shape)
        .def_rw("strength", &RimSmoothing::strength)
        .def_rw("fade", &RimSmoothing::fade);
    nb::class_<RimSmoothingFilter, Filter>(m, "RimSmoothingFilter",
                                           "Smooths the rims of the craters the layers applied.")
        .def(nb::init<RimSmoothing, Surface>(), "options"_a = RimSmoothing{},
             "surface"_a = Surface{});
}

void BindEvaluation(nb::module_& m) {
    nb::class_<PyTerrain>(m, "Terrain",
                          "A terrain: heights from `source`, plus each of `layers` in order, then "
                          "each of `filters`.")
        .def(
            "__init__",
            [](PyTerrain* self, std::shared_ptr<Source> source,
               std::vector<std::shared_ptr<Layer>> layers,
               std::vector<std::shared_ptr<Filter>> filters, const Surface& surface) {
                new (self)
                    PyTerrain{std::move(source), std::move(layers), std::move(filters), surface};
            },
            "source"_a, "layers"_a = std::vector<std::shared_ptr<Layer>>{},
            "filters"_a = std::vector<std::shared_ptr<Filter>>{}, "surface"_a = Surface{},
            "`surface` must be the one the layers were built for.")
        .def_rw("surface", &PyTerrain::surface)
        .def_rw("source", &PyTerrain::source)
        .def_rw("layers", &PyTerrain::layers)
        .def_rw("filters", &PyTerrain::filters);

    nb::class_<EvaluatedTerrain>(m, "EvaluatedTerrain",
                                 "Heights over a grid and the craters and boulders placed there.")
        .def_prop_ro(
            "heights", [](EvaluatedTerrain& terrain) -> Raster<float>& { return terrain.heights; },
            nb::rv_policy::reference_internal)
        .def_prop_ro(
            "craters",
            [](const EvaluatedTerrain& terrain) {
                return CraterTable(terrain.features.craters, terrain.features.craterAttributes);
            },
            "The craters applied, as NumPy columns 'x', 'y', 'diameter', 'age_gyr', 'id' and "
            "'manual', and 'floor_depth', 'rim_height', 'depth_ratio', 'freshness' and 'complex'.")
        .def_prop_ro(
            "boulders",
            [](const EvaluatedTerrain& terrain) { return BoulderTable(terrain.features.boulders); },
            "The boulders placed, as NumPy columns 'x', 'y', 'ground', 'diameter', 'height', "
            "'burial', 'yaw', 'lean', 'id' and 'model'.")
        .def_prop_ro(
            "boulder_model_count",
            [](const EvaluatedTerrain& terrain) { return terrain.features.boulderModels.size(); },
            "How many boulders were placed as 3D boulder models.")
        .def_prop_ro(
            "boulder_models",
            [](const EvaluatedTerrain& terrain) { return terrain.features.boulderModels; },
            "The boulders placed as 3D models, with their shapes and placements.")
        .def_prop_ro(
            "boulder_list",
            [](const EvaluatedTerrain& terrain) { return terrain.features.boulders; },
            "The boulders placed, as Boulder objects.")
        .def_prop_ro(
            "crater_list", [](const EvaluatedTerrain& terrain) { return terrain.features.craters; },
            "The craters applied, as a CraterList.")
        .def(
            "write_crater_list",
            [](const EvaluatedTerrain& terrain, const std::filesystem::path& path) {
                const ListFormat format =
                    path.extension() == ".json" ? ListFormat::Json : ListFormat::Csv;
                WriteCraterList(terrain.features.craters, path, format,
                                terrain.features.craterAttributes);
            },
            "path"_a, "Writes the craters as CSV, or JSON for a .json path.")
        .def(
            "write_boulder_list",
            [](const EvaluatedTerrain& terrain, const std::filesystem::path& path) {
                const ListFormat format =
                    path.extension() == ".json" ? ListFormat::Json : ListFormat::Csv;
                WriteBoulderList(terrain.features.boulders, path, format);
            },
            "path"_a, "Writes the boulders as CSV, or JSON for a .json path.")
        .def(
            "write_boulder_models",
            [](const EvaluatedTerrain& terrain, const std::filesystem::path& path) {
                WriteBoulderModels(terrain.features.boulderModels, path);
            },
            "path"_a, "Writes the boulder models as a glTF scene (.glb) or one PLY mesh (.ply).");

    m.def(
        "evaluate",
        [](const PyTerrain& terrain, double spacing, unsigned threads,
           std::optional<RasterGrid> grid) {
            const Terrain built = terrain.Get();
            if (!built.source) {
                throw std::invalid_argument("The terrain has no source");
            }
            return Evaluate(built, grid.value_or(built.source->Grid()),
                            EvaluationOptions{spacing, threads});
        },
        "terrain"_a, "spacing"_a = 0.0, "threads"_a = 0, "grid"_a = nb::none(),
        nb::call_guard<nb::gil_scoped_release>(),
        "Evaluates `terrain` over `grid` (by default its source's) at a sample spacing of "
        "`spacing` metres (0 for the grid's), on up to `threads` threads (0 for every hardware "
        "thread; the result does not depend on it).");
    m.def(
        "evaluate_at",
        [](const PyTerrain& terrain, double x, double y, double spacing) {
            return EvaluateAt(terrain.Get(), x, y, spacing);
        },
        "terrain"_a, "x"_a, "y"_a, "spacing"_a, nb::call_guard<nb::gil_scoped_release>(),
        "The terrain's height at (x, y) sampled every `spacing` metres; equal to the cell centred "
        "there of `evaluate`. Terrains with filters cannot be queried at points.");

    nb::class_<TileKey>(m, "TileKey", "Tile (x, y) of a tile scheme's level `level`.")
        .def(
            "__init__",
            [](TileKey* self, int level, std::int64_t x, std::int64_t y) {
                new (self) TileKey{level, x, y};
            },
            "level"_a, "x"_a, "y"_a)
        .def_rw("level", &TileKey::level)
        .def_rw("x", &TileKey::x)
        .def_rw("y", &TileKey::y)
        .def(nb::self == nb::self)
        .def("__repr__",
             [](const TileKey& k) {
                 return "TileKey(" + std::to_string(k.level) + ", " + std::to_string(k.x) + ", " +
                        std::to_string(k.y) + ")";
             })
        .def_prop_ro("parent", &TileScheme::Parent)
        .def_prop_ro("children", [](const TileKey& key) {
            const auto c = TileScheme::Children(key);
            return std::vector<TileKey>(c.begin(), c.end());
        });
    nb::class_<TileScheme>(
        m, "TileScheme",
        "A quadtree of north-up tiles: level 0 has tiles `root_size` across from `origin` (the "
        "top-left corner of tile (0, 0)), each level halves them, and every tile has `cells` x "
        "`cells` samples.")
        .def(
            "__init__",
            [](TileScheme* self, std::pair<double, double> origin, double rootSize,
               std::size_t cells) {
                new (self) TileScheme{ToVec(origin), rootSize, cells};
                self->Validate();
            },
            "origin"_a, "root_size"_a, "cells"_a = 256)
        .def_static("covering", &TileScheme::Covering, "grid"_a, "cells"_a = 256,
                    "A scheme one of whose levels has the cells of `grid`.")
        .def_static("whole_body", &TileScheme::WholeBody, "cells"_a = 256,
                    "A longitude-latitude pyramid over a whole sphere.")
        .def_static("body_tiles", &TileScheme::BodyTiles, "level"_a,
                    "Every tile of a level of a whole-body scheme.")
        .def_static("wrap_body_tile", &TileScheme::WrapBodyTile, "key"_a,
                    "The whole-body tile a neighbour across the antimeridian is.")
        .def_prop_ro("origin",
                     [](const TileScheme& s) { return std::pair{s.origin.x, s.origin.y}; })
        .def_ro("root_size", &TileScheme::rootSize)
        .def_ro("cells", &TileScheme::cells)
        .def("tile_size", &TileScheme::TileSize, "level"_a)
        .def("spacing", &TileScheme::Spacing, "level"_a)
        .def("level_for", &TileScheme::LevelFor, "spacing"_a,
             "The coarsest level whose spacing is at most `spacing`.")
        .def("tile_at", &TileScheme::TileAt, "x"_a, "y"_a, "level"_a)
        .def(
            "tiles_covering",
            [](const TileScheme& s, std::tuple<double, double, double, double> bounds, int level) {
                return s.TilesCovering(BoundsOf(bounds), level);
            },
            "bounds"_a, "level"_a)
        .def("grid", &TileScheme::Grid, "key"_a, "The tile's grid of samples.");
    m.def(
        "evaluate_tile",
        [](const PyTerrain& terrain, const TileScheme& scheme, const TileKey& key,
           unsigned threads) { return EvaluateTile(terrain.Get(), scheme, key, threads); },
        "terrain"_a, "scheme"_a, "key"_a, "threads"_a = 1, nb::call_guard<nb::gil_scoped_release>(),
        "One tile of `scheme` at its level's spacing, equal to the same cells of any larger grid.");
    m.def(
        "evaluate_tiles",
        [](const PyTerrain& terrain, const TileScheme& scheme, const std::vector<TileKey>& keys,
           unsigned threads) { return EvaluateTiles(terrain.Get(), scheme, keys, threads); },
        "terrain"_a, "scheme"_a, "keys"_a, "threads"_a = 0,
        nb::call_guard<nb::gil_scoped_release>(),
        "`evaluate_tile` for each of `keys`, split over threads.");
    m.def(
        "filter_margin",
        [](const PyTerrain& terrain, double spacing) {
            return FilterMargin(terrain.Get(), spacing);
        },
        "terrain"_a, "spacing"_a,
        "How far (m) the terrain's filters read around a grid evaluated at `spacing`.");

    m.def(
        "body_mesh",
        [](const PyTerrain& terrain, std::size_t cellsPerFace, double spacing, unsigned threads) {
            return BodyMesh(terrain.Get(), cellsPerFace, spacing, threads);
        },
        "terrain"_a, "cells_per_face"_a, "spacing"_a = 0.0, "threads"_a = 0,
        nb::call_guard<nb::gil_scoped_release>(),
        "A mesh of the whole body a terrain on a sphere lies on: six cube faces of "
        "`cells_per_face` squared quads, each vertex at the radius plus the terrain's height "
        "there. Filters are left out.");

    nb::class_<TileCache>(
        m, "TileCache",
        "Tiles of one terrain and scheme, evaluated on demand and kept up to about "
        "`max_bytes`, least recently used first out; thread-safe. The budget is "
        "split over 16 shards that each keep at least one tile, so a budget "
        "smaller than 16 tiles is exceeded.")
        .def(
            "__init__",
            [](TileCache* self, const PyTerrain& terrain, const TileScheme& scheme,
               std::size_t maxBytes) { new (self) TileCache(terrain.Get(), scheme, maxBytes); },
            "terrain"_a, "scheme"_a, "max_bytes"_a = std::size_t{1} << 30)
        .def(
            "get",
            [](TileCache& cache, const TileKey& key) { return EvaluatedTerrain(*cache.Get(key)); },
            "key"_a, nb::call_guard<nb::gil_scoped_release>(),
            "The tile at `key`, evaluated if it is not held.")
        .def_prop_ro("size", &TileCache::Size, "Bytes held.")
        .def_prop_ro("max_bytes", &TileCache::MaxBytes)
        .def_prop_ro("evictions", &TileCache::Evictions, "Tiles evicted so far.");

    nb::class_<ExposureField>(
        m, "ExposureField",
        "How exposed the ground is, from 0 (buried under regolith) to 1 (bare boulder): crests and "
        "steep slopes shed regolith, read over `params.reach` metres.")
        .def(nb::init<ExposureParams, Surface>(), "params"_a = ExposureParams{},
             "surface"_a = Surface{})
        .def(
            "fill",
            [](const ExposureField& field, const PyTerrain& terrain, std::size_t layers,
               const RasterGrid& grid, double spacing, unsigned threads) {
                const Terrain built = terrain.Get();
                const TerrainGround ground(built, layers);
                Raster<float> exposure = grid.MakeRaster();
                field.Fill(exposure, ground, spacing, threads);
                return exposure;
            },
            "terrain"_a, "layers"_a, "grid"_a, "spacing"_a, "threads"_a = 0,
            nb::call_guard<nb::gil_scoped_release>(),
            "The exposure over `grid` of the ground made by `terrain`'s source and its first "
            "`layers` layers, sampled every `spacing` metres.")
        .def(
            "exposure",
            [](const ExposureField& field, const PyTerrain& terrain, std::size_t layers, double x,
               double y, double spacing) {
                const Terrain built = terrain.Get();
                return field.Exposure(TerrainGround(built, layers), x, y, spacing);
            },
            "terrain"_a, "layers"_a, "x"_a, "y"_a, "spacing"_a);
}

}  // namespace

void BindTerrain(nb::module_& m) {
    BindSurface(m);
    BindGridAndContext(m);
    BindSources(m);
    BindLayerBase(m);
    BindCraterLayers(m);
    BindBoulderShapes(m);
    BindBoulderLayers(m);
    BindFilters(m);
    BindEvaluation(m);
}

}  // namespace terraingen::python
