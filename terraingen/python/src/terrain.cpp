#include "bindings.h"
#include "options.h"
#include "tables.h"

#include "terraingen/terraingen.h"

#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
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

/// A terrain as Python builds it: the parts are held as mutable pointers so they can be passed
/// back to Python, and converted to a `Terrain` for evaluation.
struct PyTerrain {
    std::shared_ptr<Source> source;
    std::vector<std::shared_ptr<Layer>> layers;
    std::vector<std::shared_ptr<Filter>> filters;

    Terrain Get() const {
        Terrain terrain;
        terrain.source = source;
        terrain.layers.assign(layers.begin(), layers.end());
        terrain.filters.assign(filters.begin(), filters.end());
        return terrain;
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
        .def_prop_ro("bounds", [](const RasterGrid& grid) {
            const Bounds2d b = grid.Bounds();
            return std::tuple{b.minX, b.minY, b.maxX, b.maxY};
        });

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
               float elevation, std::pair<double, double> centre) {
                new (self) FlatSource(width, height, pixelSize, elevation, ToVec(centre));
            },
            "width"_a, "height"_a, "pixel_size"_a, "elevation"_a = 0.0f,
            "centre"_a = std::pair{0.0, 0.0});

    nb::class_<GeoTiffSource, Source>(
        m, "GeoTiffSource",
        "Heights from band `band` of a GeoTIFF, optionally resampled to `pixel_size` metres and "
        "scaled (height * scale + offset).")
        .def(
            "__init__",
            [](GeoTiffSource* self, const std::filesystem::path& path, std::size_t band,
               std::optional<double> pixelSize) {
                new (self) GeoTiffSource(path, band, pixelSize);
            },
            "path"_a, "band"_a = 0, "pixel_size"_a = nb::none());
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
            [](NoiseRelief* self, std::vector<NoiseLayer> layers,
               std::pair<double, double> origin) {
                new (self) NoiseRelief(SyntheticTerrainParams{std::move(layers)}, ToVec(origin));
            },
            "layers"_a, "origin"_a = std::pair{0.0, 0.0});

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
               BlendShape blend) {
                new (self) RegionLayer(std::move(layer), shape, feather, blend);
            },
            "layer"_a, "shape"_a, "feather"_a = 0.0, "blend"_a = BlendShape::Smoothstep)
        .def("weight", &RegionLayer::Weight, "x"_a, "y"_a);

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
        .def(nb::init<RoughnessOptions>(), "options"_a = RoughnessOptions{})
        .def("rms_slope", &RoughnessLayer::RmsSlope, "baseline"_a);

    auto exposure = nb::class_<ExposureParams>(
        m, "ExposureParams",
        "How exposed the ground is (crests and steep slopes shed regolith): rises and slopes "
        "for soil and rock, read over `reach` metres.");
    BindKwargsInit<ExposureParams>(exposure);
    exposure.def_rw("reach", &ExposureParams::reach)
        .def_rw("rise_soil", &ExposureParams::riseSoil)
        .def_rw("rise_rock", &ExposureParams::riseRock)
        .def_rw("slope_soil", &ExposureParams::slopeSoil)
        .def_rw("slope_rock", &ExposureParams::slopeRock)
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
        .def(nb::init<BedformOptions>(), "options"_a)
        .def("cover_at",
             [](const BedformLayer& layer, double x, double y) { return layer.CoverAt(x, y); });
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

    auto spacing = nb::class_<CraterSpacing>(
        m, "CraterSpacing",
        "Keeps craters of similar size apart: two conflict when their diameters differ by at most "
        "`diameter_ratio` (at least 1) and their centres are closer than `separation` times the "
        "larger diameter. A separation of 0 disables it.");
    BindKwargsInit<CraterSpacing>(spacing);
    spacing.def_rw("diameter_ratio", &CraterSpacing::diameterRatio)
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
        .def_rw("shape", &CraterBlend::shape);

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
    auto irregularity = nb::class_<CraterIrregularity>(
        m, "CraterIrregularity",
        "Irregular, elongated crater outlines for the craters it selects: rims roughened by "
        "random harmonics and some craters stretched into ellipses.");
    BindKwargsInit<CraterIrregularity>(irregularity);
    irregularity.def_rw("enabled", &CraterIrregularity::enabled)
        .def_rw("seed", &CraterIrregularity::seed)
        .def_rw("min_diameter", &CraterIrregularity::minDiameter)
        .def_rw("max_diameter", &CraterIrregularity::maxDiameter)
        .def_rw("min_age_gyr", &CraterIrregularity::minAgeGyr)
        .def_rw("max_age_gyr", &CraterIrregularity::maxAgeGyr)
        .def_rw("manual", &CraterIrregularity::manual)
        .def_rw("rough_share", &CraterIrregularity::roughShare)
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
        "How craters are applied: their `order`, the `datum` interiors are cut relative to (fit "
        "over `datum_fit_radii` radii), how much of the interior replaces the terrain "
        "(`interior_replacement`, 0 to 1), the `blend` into the exterior, the resolution `fade` "
        "and rim `irregularity`.");
    BindKwargsInit<CraterApplyOptions>(apply);
    apply.def_rw("order", &CraterApplyOptions::order)
        .def_rw("fade", &CraterApplyOptions::fade)
        .def_rw("datum", &CraterApplyOptions::datum)
        .def_rw("datum_fit_radii", &CraterApplyOptions::datumFitRadii)
        .def_rw("interior_replacement", &CraterApplyOptions::interiorReplacement)
        .def_rw("blend", &CraterApplyOptions::blend)
        .def_rw("irregularity", &CraterApplyOptions::irregularity);

    auto options = nb::class_<CraterLayerOptions>(
        m, "CraterLayerOptions",
        "A crater layer's settings: whether to `generate` craters from the density, its `seed`, "
        "the `generation` options and the `apply` options.");
    BindKwargsInit<CraterLayerOptions>(options);
    options.def_rw("generate", &CraterLayerOptions::generate)
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
        .def_rw("min_primary_diameter", &SecondaryCraterOptions::minPrimaryDiameter)
        .def_rw("max_primary_diameter", &SecondaryCraterOptions::maxPrimaryDiameter)
        .def_rw("min_age_gyr", &SecondaryCraterOptions::minAgeGyr)
        .def_rw("max_age_gyr", &SecondaryCraterOptions::maxAgeGyr)
        .def_rw("manual", &SecondaryCraterOptions::manual);
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
}

void BindRockLayers(nb::module_& m) {
    auto patches = nb::class_<RockPatches>(
        m, "RockPatches",
        "Patches of richer and poorer rock cover, about `size` metres across: the abundance "
        "varies from 1 / `spread` to `spread` times its mean (1 for the same everywhere).");
    BindKwargsInit<RockPatches>(patches);
    patches.def_rw("spread", &RockPatches::spread).def_rw("size", &RockPatches::size);

    nb::enum_<EjectaProfile>(m, "EjectaProfile", "How ejecta rocks fall off from a crater's rim.")
        .value("SMOOTHSTEP", EjectaProfile::Smoothstep)
        .value("EXPONENTIAL", EjectaProfile::Exponential);
    nb::enum_<EjectaAging>(m, "EjectaAging", "How ejecta rocks fade as their crater ages.")
        .value("FRESHNESS", EjectaAging::Freshness)
        .value("AGE", EjectaAging::Age);
    auto ejecta = nb::class_<RockEjecta>(
        m, "RockEjecta",
        "Rocks thrown out by craters at least `min_crater_diameter` across (or deep enough to "
        "reach bedrock under `regolith_thickness`): extra `coverage` near the rim, falling off "
        "to `reach` radii, fading as the crater ages.");
    BindKwargsInit<RockEjecta>(ejecta);
    ejecta.def_rw("min_crater_diameter", &RockEjecta::minCraterDiameter)
        .def_rw("regolith_thickness", &RockEjecta::regolithThickness)
        .def_rw("excavation_depth_ratio", &RockEjecta::excavationDepthRatio)
        .def_rw("distribution", &RockEjecta::distribution)
        .def_rw("coverage", &RockEjecta::coverage)
        .def_rw("profile", &RockEjecta::profile)
        .def_rw("reach", &RockEjecta::reach)
        .def_rw("decay_radii", &RockEjecta::decayRadii)
        .def_rw("floor_share", &RockEjecta::floorShare)
        .def_rw("aging", &RockEjecta::aging)
        .def_rw("min_freshness", &RockEjecta::minFreshness)
        .def_rw("lifetime_gyr", &RockEjecta::lifetimeGyr)
        .def_rw("aging_exponent", &RockEjecta::agingExponent);

    nb::enum_<RockRelief>(m, "RockRelief", "How rocks are drawn into the heights.")
        .value("DOME", RockRelief::Dome)
        .value("SHAPE", RockRelief::Shape)
        .value("NONE", RockRelief::None);
    auto models = nb::class_<RockModels>(
        m, "RockModels",
        "Which rocks are placed as 3D boulder models: at least `min_diameter` metres across, or "
        "`min_samples` samples across.");
    BindKwargsInit<RockModels>(models);
    models.def_rw("min_diameter", &RockModels::minDiameter)
        .def_rw("min_samples", &RockModels::minSamples);

    auto options = nb::class_<RockLayerOptions>(
        m, "RockLayerOptions",
        "A rock layer's settings: sizes from Golombek and Rapp's model for `coverage` (or "
        "`distribution`), between `min_diameter` and `max_diameter`; `patches`, `ejecta` and "
        "`exposure` shape where they lie; heights, burial and lean shape each rock; `relief` and "
        "`models` how they are drawn.");
    BindKwargsInit<RockLayerOptions>(options);
    options.def_rw("coverage", &RockLayerOptions::coverage)
        .def_rw("qa", &RockLayerOptions::qa)
        .def_rw("qb", &RockLayerOptions::qb)
        .def_rw("distribution", &RockLayerOptions::distribution)
        .def_rw("min_diameter", &RockLayerOptions::minDiameter)
        .def_rw("max_diameter", &RockLayerOptions::maxDiameter)
        .def_rw("seed", &RockLayerOptions::seed)
        .def_rw("patches", &RockLayerOptions::patches)
        .def_rw("ejecta", &RockLayerOptions::ejecta)
        .def_rw("exposure", &RockLayerOptions::exposure)
        .def_rw("bedrock_abundance", &RockLayerOptions::bedrockAbundance)
        .def_rw("exposure_spacing", &RockLayerOptions::exposureSpacing)
        .def_rw("min_height_ratio", &RockLayerOptions::minHeightRatio)
        .def_rw("max_height_ratio", &RockLayerOptions::maxHeightRatio)
        .def_rw("min_burial", &RockLayerOptions::minBurial)
        .def_rw("max_burial", &RockLayerOptions::maxBurial)
        .def_rw("max_lean", &RockLayerOptions::maxLean)
        .def_rw("relief", &RockLayerOptions::relief)
        .def_rw("models", &RockLayerOptions::models)
        .def_rw("fade", &RockLayerOptions::fade);

    nb::class_<RockLayer, Layer>(
        m, "RockLayer",
        "Rocks drawn into the heights, the largest optionally placed as 3D boulders; `craters` "
        "supplies the craters that throw ejecta rocks.")
        .def(
            "__init__",
            [](RockLayer* self, RockLayerOptions options, std::shared_ptr<CraterLayer> craters) {
                new (self) RockLayer(std::move(options), std::move(craters));
            },
            "options"_a = RockLayerOptions{}, "craters"_a = nb::none())
        .def(
            "rocks",
            [](const RockLayer& layer, std::tuple<double, double, double, double> region,
               double spacing) { return RockTable(layer.Rocks(BoundsOf(region), spacing)); },
            "region"_a, "spacing"_a = 0.0,
            "The rocks over `region` (min_x, min_y, max_x, max_y) as NumPy columns, without "
            "evaluating any heights or ground.")
        .def(
            "abundance_at",
            [](const RockLayer& layer, double x, double y) {
                const RockAbundance abundance = layer.AbundanceAt(x, y);
                return std::pair{abundance.background, abundance.ejecta};
            },
            "x"_a, "y"_a, "The rock abundance at (x, y): (background, ejecta).")
        .def_prop_ro("options", &RockLayer::Options);
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
    nb::class_<RimSmoothingFilter, Filter>(m, "RimSmoothingFilter",
                                           "Smooths crater rims with a Gaussian of `sigma_radii` "
                                           "crater radii, at `strength` from 0 to 1.")
        .def(
            "__init__",
            [](RimSmoothingFilter* self, double sigmaRadii, double strength) {
                RimSmoothing smoothing;
                smoothing.sigmaRadii = sigmaRadii;
                smoothing.strength = strength;
                new (self) RimSmoothingFilter(smoothing);
            },
            "sigma_radii"_a = 0.05, "strength"_a = 1.0);
}

void BindEvaluation(nb::module_& m) {
    nb::class_<PyTerrain>(m, "Terrain",
                          "A terrain: heights from `source`, plus each of `layers` in order, then "
                          "each of `filters`.")
        .def(
            "__init__",
            [](PyTerrain* self, std::shared_ptr<Source> source,
               std::vector<std::shared_ptr<Layer>> layers,
               std::vector<std::shared_ptr<Filter>> filters) {
                new (self) PyTerrain{std::move(source), std::move(layers), std::move(filters)};
            },
            "source"_a, "layers"_a = std::vector<std::shared_ptr<Layer>>{},
            "filters"_a = std::vector<std::shared_ptr<Filter>>{})
        .def_rw("source", &PyTerrain::source)
        .def_rw("layers", &PyTerrain::layers)
        .def_rw("filters", &PyTerrain::filters);

    nb::class_<EvaluatedTerrain>(m, "EvaluatedTerrain",
                                 "Heights over a grid and the craters and rocks placed there.")
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
            "rocks",
            [](const EvaluatedTerrain& terrain) { return RockTable(terrain.features.rocks); },
            "The rocks placed, as NumPy columns 'x', 'y', 'ground', 'diameter', 'height', "
            "'burial', 'yaw', 'lean', 'id' and 'model'.")
        .def_prop_ro(
            "boulder_count",
            [](const EvaluatedTerrain& terrain) { return terrain.features.boulders.size(); },
            "How many rocks were placed as 3D boulder models.")
        .def(
            "write_crater_list",
            [](const EvaluatedTerrain& terrain, const std::filesystem::path& path) {
                const CraterListFormat format =
                    path.extension() == ".json" ? CraterListFormat::Json : CraterListFormat::Csv;
                WriteCraterList(terrain.features.craters, path, format,
                                terrain.features.craterAttributes);
            },
            "path"_a, "Writes the craters as CSV, or JSON for a .json path.")
        .def(
            "write_rock_list",
            [](const EvaluatedTerrain& terrain, const std::filesystem::path& path) {
                const CraterListFormat format =
                    path.extension() == ".json" ? CraterListFormat::Json : CraterListFormat::Csv;
                WriteRockList(terrain.features.rocks, path, format);
            },
            "path"_a, "Writes the rocks as CSV, or JSON for a .json path.")
        .def(
            "write_boulder_models",
            [](const EvaluatedTerrain& terrain, const std::filesystem::path& path) {
                WriteBoulderModels(terrain.features.boulders, path);
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
}

}  // namespace

void BindTerrain(nb::module_& m) {
    BindGridAndContext(m);
    BindSources(m);
    BindLayerBase(m);
    BindCraterLayers(m);
    BindRockLayers(m);
    BindFilters(m);
    BindEvaluation(m);
}

}  // namespace terraingen::python
