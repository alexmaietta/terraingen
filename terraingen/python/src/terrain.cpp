#include "bindings.h"
#include "tables.h"

#include "terraingen/terraingen.h"

#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace nb = nanobind;
using namespace nb::literals;

namespace terraingen::python {

namespace {

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

/// An albedo as Python builds it; see `PyTerrain`.
struct PyAlbedo {
    std::shared_ptr<AlbedoSource> source;
    std::vector<std::shared_ptr<AlbedoLayer>> layers;

    Albedo Get() const {
        Albedo albedo;
        albedo.source = source;
        albedo.layers.assign(layers.begin(), layers.end());
        return albedo;
    }
};

Vec2d ToVec(const std::pair<double, double>& p) {
    return {p.first, p.second};
}

void BindSources(nb::module_& m) {
    nb::class_<Source>(m, "Source", "Where a terrain's heights come from, over its grid.")
        .def_prop_ro(
            "width", [](const Source& source) { return source.Grid().width; },
            "Grid width in cells.")
        .def_prop_ro(
            "height", [](const Source& source) { return source.Grid().height; },
            "Grid height in cells.")
        .def_prop_ro(
            "transform", [](const Source& source) { return source.Grid().transform; },
            "The grid's geotransform.")
        .def("height_at", &Source::HeightAt, "x"_a, "y"_a, "The source's height at (x, y).");

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
}

void BindLayers(nb::module_& m) {
    nb::class_<Layer>(m, "Layer", "Relief added on top of a terrain's source.");

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
        .def_rw("manual", &Crater::manual);

    nb::class_<SizeFrequencyDistribution>(m, "SizeFrequencyDistribution",
                                          "A cumulative size-frequency distribution of craters or "
                                          "rocks per square metre, diameters in metres.")
        .def("cumulative", &SizeFrequencyDistribution::Cumulative, "diameter"_a,
             "Number per square metre at least `diameter` across.")
        .def("expected_count", &SizeFrequencyDistribution::ExpectedCount, "area"_a,
             "min_diameter"_a, "max_diameter"_a,
             "Expected number over `area` square metres between the diameters.");
    nb::class_<PowerLawSfd, SizeFrequencyDistribution>(
        m, "PowerLawSfd", "N(>= D) = coefficient * D^-exponent per square metre, D in metres.")
        .def(
            "__init__",
            [](PowerLawSfd* self, double coefficient, double exponent) {
                new (self) PowerLawSfd(coefficient, exponent);
            },
            "coefficient"_a, "exponent"_a);

    nb::class_<CraterAgeModel>(m, "CraterAgeModel", "How old each crater is.");
    nb::class_<FreshAges, CraterAgeModel>(m, "FreshAges", "Every crater is fresh (age 0).")
        .def(nb::init<>());
    nb::class_<UniformAges, CraterAgeModel>(m, "UniformAges",
                                            "Ages uniform over a lifetime of `lifetime_gyr`.")
        .def(nb::init<double>(), "lifetime_gyr"_a);

    nb::class_<SurfaceModel>(m, "SurfaceModel", "A surface's crater density and age model.")
        .def_prop_ro("density",
                     [](const SurfaceModel& model) {
                         return std::const_pointer_cast<SizeFrequencyDistribution>(model.density);
                     })
        .def_prop_ro("ages", [](const SurfaceModel& model) {
            return std::const_pointer_cast<CraterAgeModel>(model.ages);
        });
    m.def("lunar_surface", &LunarSurface, "age_gyr"_a,
          "The DSNE lunar surface `age_gyr` old: the Neukum production capped at Trask's "
          "equilibrium, valid from 2.5 m to 300 km, with steady-state ages.");
    m.def("mars_surface", &MarsSurface, "age_gyr"_a,
          "A Martian surface `age_gyr` old: Ivanov's chronology and Hartmann's production, valid "
          "from 15 m to 362 km.");

    nb::class_<DegradationModel>(m, "DegradationModel", "How a crater's shape changes with age.");
    nb::class_<NoDegradation, DegradationModel>(
        m, "NoDegradation", "Craters keep their fresh lunar shape, or a fixed depth ratio.")
        .def(
            "__init__",
            [](NoDegradation* self, std::optional<double> depthRatio) {
                new (self) NoDegradation(LunarFreshCraterMorphometry, depthRatio);
            },
            "depth_ratio"_a = nb::none());
    nb::class_<FreshnessDegradation, DegradationModel>(
        m, "FreshnessDegradation",
        "Craters shallow from their fresh lunar shape as they age, relative to the lifetime "
        "`ages` gives their size, down to `cutoff_depth_ratio`.")
        .def(
            "__init__",
            [](FreshnessDegradation* self, std::shared_ptr<CraterAgeModel> ages,
               double cutoffDepthRatio) {
                new (self) FreshnessDegradation(std::move(ages), LunarFreshCraterMorphometry,
                                                cutoffDepthRatio);
            },
            "ages"_a, "cutoff_depth_ratio"_a = 0.04);

    nb::enum_<FassettThomsonRim>(m, "FassettThomsonRim", "How the Fassett and Thomson rim is fit.")
        .value("ORIGINAL", FassettThomsonRim::Original)
        .value("AT_ONE_RADIUS", FassettThomsonRim::AtOneRadius)
        .value("PLATEAU", FassettThomsonRim::Plateau);
    nb::class_<CraterProfile>(m, "CraterProfile", "A crater's radial height profile.");
    nb::class_<FassettThomsonProfile, CraterProfile>(
        m, "FassettThomsonProfile",
        "The fresh simple-crater profile of Fassett and Thomson (2014).")
        .def(nb::init<FassettThomsonRim>(), "rim"_a = FassettThomsonRim::Original);
    nb::class_<MartinParkesDunstanProfile, CraterProfile>(
        m, "MartinParkesDunstanProfile",
        "A profile with a rounded rim that lowers as the crater degrades.")
        .def(nb::init<>());

    nb::class_<CraterModels>(m, "CraterModels",
                             "The models a crater layer draws and shapes craters with.")
        .def(
            "__init__",
            [](CraterModels* self, std::shared_ptr<SizeFrequencyDistribution> density,
               std::shared_ptr<CraterAgeModel> ages, std::shared_ptr<DegradationModel> degradation,
               std::shared_ptr<CraterProfile> profile) {
                new (self) CraterModels{std::move(density), std::move(ages), std::move(degradation),
                                        std::move(profile)};
            },
            "density"_a, "ages"_a, "degradation"_a, "profile"_a);

    nb::class_<CraterLayer, Layer>(m, "CraterLayer",
                                   "Craters drawn from `models.density` between the diameters "
                                   "(m), plus any `manual` craters, applied to the terrain.")
        .def(
            "__init__",
            [](CraterLayer* self, const CraterModels& models, std::uint32_t seed, bool generate,
               double minDiameter, double maxDiameter, const std::vector<Crater>& manual) {
                CraterLayerOptions options;
                options.seed = seed;
                options.generate = generate;
                options.generation.dMin = minDiameter;
                options.generation.dMax = maxDiameter;
                for (Crater crater : manual) {
                    crater.manual = true;
                    options.generation.manual.Append(crater);
                }
                new (self) CraterLayer(models, options);
            },
            "models"_a, "seed"_a = 1, "generate"_a = true, "min_diameter"_a = 4.0,
            "max_diameter"_a = 250.0, "manual"_a = std::vector<Crater>{});

    nb::class_<RockLayer, Layer>(m, "RockLayer",
                                 "Rocks from Golombek and Rapp's (1997) model for `coverage` "
                                 "(the fractional area covered), drawn into the heights; those at "
                                 "least `model_min_diameter` across are placed as 3D boulders.")
        .def(
            "__init__",
            [](RockLayer* self, double coverage, double minDiameter, double maxDiameter,
               std::uint32_t seed, std::optional<double> modelMinDiameter,
               std::shared_ptr<CraterLayer> craters) {
                RockLayerOptions options;
                options.coverage = coverage;
                options.minDiameter = minDiameter;
                options.maxDiameter = maxDiameter;
                options.seed = seed;
                options.models.minDiameter = modelMinDiameter;
                new (self) RockLayer(options, std::move(craters));
            },
            "coverage"_a = 0.02, "min_diameter"_a = 0.1, "max_diameter"_a = 4.0, "seed"_a = 1,
            "model_min_diameter"_a = nb::none(), "craters"_a = nb::none());
}

void BindFilters(nb::module_& m) {
    nb::class_<Filter>(m, "Filter", "A transform of a terrain's heights after its layers.");
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
            "The rocks placed, as NumPy columns (see GeneratedTerrain.rocks).")
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
        [](const PyTerrain& terrain, double spacing, unsigned threads) {
            const Terrain built = terrain.Get();
            if (!built.source) {
                throw std::invalid_argument("The terrain has no source");
            }
            return Evaluate(built, built.source->Grid(), EvaluationOptions{spacing, threads});
        },
        "terrain"_a, "spacing"_a = 0.0, "threads"_a = 0, nb::call_guard<nb::gil_scoped_release>(),
        "Evaluates `terrain` over its source's grid at a sample spacing of `spacing` metres (0 for "
        "the grid's), on up to `threads` threads (0 for every hardware thread; the result does "
        "not depend on it).");
    m.def(
        "evaluate_at",
        [](const PyTerrain& terrain, double x, double y, double spacing) {
            return EvaluateAt(terrain.Get(), x, y, spacing);
        },
        "terrain"_a, "x"_a, "y"_a, "spacing"_a,
        "The terrain's height at (x, y) sampled every `spacing` metres; equal to the cell centred "
        "there of `evaluate`. Terrains with filters cannot be queried at points.");
}

void BindAlbedo(nb::module_& m) {
    nb::class_<AlbedoSource>(m, "AlbedoSource", "Where an albedo starts.");
    nb::class_<SyntheticAlbedo, AlbedoSource>(
        m, "SyntheticAlbedo",
        "A synthetic albedo: `value` (reflectance), varied by `value_noise` (relative), and with "
        "`rgb` three bands from `hue` and `saturation` and their noise.")
        .def(
            "__init__",
            [](SyntheticAlbedo* self, double value, std::vector<NoiseLayer> valueNoise, bool rgb,
               double hue, double saturation, std::vector<NoiseLayer> hueNoise,
               std::vector<NoiseLayer> saturationNoise) {
                SyntheticAlbedoParams params;
                params.value = value;
                params.valueNoise = std::move(valueNoise);
                params.rgb = rgb;
                params.hue = hue;
                params.saturation = saturation;
                params.hueNoise = std::move(hueNoise);
                params.saturationNoise = std::move(saturationNoise);
                new (self) SyntheticAlbedo(std::move(params));
            },
            "value"_a = SyntheticAlbedoParams{}.value, "value_noise"_a = std::vector<NoiseLayer>{},
            "rgb"_a = false, "hue"_a = 0.0, "saturation"_a = 0.0,
            "hue_noise"_a = std::vector<NoiseLayer>{},
            "saturation_noise"_a = std::vector<NoiseLayer>{});

    nb::class_<AlbedoLayer>(m, "AlbedoLayer", "A change to an albedo, such as bright craters.");
    nb::class_<CraterAlbedo, AlbedoLayer>(m, "CraterAlbedo",
                                          "Bright fresh craters and rays from the craters of "
                                          "`craters`, fading with age.")
        .def(
            "__init__",
            [](CraterAlbedo* self, std::shared_ptr<CraterLayer> craters, std::uint32_t seed,
               bool rays) {
                CraterAlbedoOptions options;
                options.seed = seed;
                options.rays.enabled = rays;
                new (self) CraterAlbedo(std::move(craters), options);
            },
            "craters"_a, "seed"_a = 0, "rays"_a = true);

    nb::class_<PyAlbedo>(m, "Albedo", "An albedo: `source`, changed by each of `layers` in order.")
        .def(
            "__init__",
            [](PyAlbedo* self, std::shared_ptr<AlbedoSource> source,
               std::vector<std::shared_ptr<AlbedoLayer>> layers) {
                new (self) PyAlbedo{std::move(source), std::move(layers)};
            },
            "source"_a, "layers"_a = std::vector<std::shared_ptr<AlbedoLayer>>{})
        .def_rw("source", &PyAlbedo::source)
        .def_rw("layers", &PyAlbedo::layers);

    m.def(
        "evaluate_albedo",
        [](const PyAlbedo& albedo, const PyTerrain& terrain, double spacing, unsigned threads) {
            if (!terrain.source) {
                throw std::invalid_argument("The terrain has no source");
            }
            return EvaluateAlbedo(albedo.Get(), terrain.source->Grid(),
                                  AlbedoContext{spacing, threads});
        },
        "albedo"_a, "terrain"_a, "spacing"_a = 0.0, "threads"_a = 0,
        nb::call_guard<nb::gil_scoped_release>(),
        "The albedo's bands over `terrain`'s source grid, sampled every `spacing` metres (0 for "
        "the grid's), one Raster per band.");

    m.def(
        "write_geotiff",
        [](const std::filesystem::path& path, const std::vector<const Raster<float>*>& bands) {
            WriteGeoTiff(path, bands);
        },
        "path"_a, "bands"_a, "Writes one or more rasters as the bands of a float32 GeoTIFF.");
    m.def(
        "write_geotiff",
        [](const std::filesystem::path& path, const Raster<float>& raster) {
            WriteGeoTiff(path, raster);
        },
        "path"_a, "raster"_a);
    m.def(
        "read_geotiff",
        [](const std::filesystem::path& path, std::size_t band) {
            return ReadGeoTiff(path, band).raster;
        },
        "path"_a, "band"_a = 0,
        "Reads band `band` (from 0) of a GeoTIFF as a Raster with the file's transform and no-data "
        "value.");
}

}  // namespace

void BindTerrain(nb::module_& m) {
    BindSources(m);
    BindLayers(m);
    BindFilters(m);
    BindEvaluation(m);
    BindAlbedo(m);
}

}  // namespace terraingen::python
