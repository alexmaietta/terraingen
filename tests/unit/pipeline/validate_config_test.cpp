/// @file
/// @brief Tests of `ValidateConfig`, `DescribeSource` and `BuildSurface` (pipeline/validate.cpp).
#include "pipeline_configs.h"
#include "support/temp_path.h"
#include "terraingen/pipeline/run.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

using terraingen::ConfigError;
using terraingen::CraterLayerSpec;
using terraingen::TerrainConfig;
using terraingen::ValidateConfig;
using terraingen::testing::Craters;
using terraingen::testing::ErrorPath;
using terraingen::testing::Flat;
using terraingen::testing::Noise;
using terraingen::testing::Small;
using terraingen::testing::TempPath;
using terraingen::testing::ValidationErrorPath;

namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();

/// A change to a `Small` configuration and the JSON path validation reports for it.
struct PathCase {
    const char* name;
    void (*mutate)(TerrainConfig&);
    const char* path;
};

const PathCase kPathCases[] = {
    {"Unchanged", [](TerrainConfig&) {}, "valid"},
    {"NarrowGrid", [](TerrainConfig& c) { Flat(c).width = 1; }, "source.width"},
    {"TallGrid", [](TerrainConfig& c) { Flat(c).height = 100000; }, "source.width"},
    {"ZeroPixelSize", [](TerrainConfig& c) { Flat(c).pixelSize = 0.0; }, "source.pixel_size"},
    {"EmptyNoiseStack", [](TerrainConfig& c) { Noise(c).stack.clear(); }, "layers[0].stack"},
    {"NoOctaves", [](TerrainConfig& c) { Noise(c).stack[0].layer.fractal.octaves = 0; },
     "layers[0].stack[0]"},
    {"InfiniteHurst", [](TerrainConfig& c) { Noise(c).stack[0].hurst = 1.0f / 0.0f; },
     "layers[0].stack[0].hurst"},
    {"HeightmapWithoutPath", [](TerrainConfig& c) { c.outputs = {terraingen::HeightmapOutput{}}; },
     "outputs[0].path"},
    {"PackBitsHeightmap",
     [](TerrainConfig& c) {
         c.outputs = {terraingen::HeightmapOutput{"a.tif", terraingen::TiffCompression::PackBits}};
     },
     "outputs[0].compression"},
    {"OddTileSize",
     [](TerrainConfig& c) {
         c.outputs = {terraingen::HeightmapOutput{"a.tif", terraingen::TiffCompression::Lzw, 20}};
     },
     "outputs[0].tile_size"},
    {"LunarSurfaceTooOld",
     [](TerrainConfig& c) { Craters(c).density = terraingen::LunarSurfaceSpec{9.0}; },
     "layers[1].density"},
    {"NoLifetime", [](TerrainConfig& c) { Craters(c).ages = terraingen::UniformAgesSpec{0.0}; },
     "layers[1].ages"},
    {"DeepCutoff",
     [](TerrainConfig& c) { Craters(c).degradation = terraingen::FreshnessDegradationSpec{0.5}; },
     "layers[1].degradation"},
    {"SpacingRatioBelowOne", [](TerrainConfig& c) { Craters(c).spacing.diameterRatio = 0.5; },
     "layers[1].spacing"},
    {"ReplacementAboveOne", [](TerrainConfig& c) { Craters(c).apply.interiorReplacement = 2.0; },
     "layers[1].apply"},
    {"NegativeRimStrength",
     [](TerrainConfig& c) {
         c.filters = {terraingen::RimSmoothing{}, terraingen::RimSmoothing{.strength = -1.0}};
     },
     "filters[1]"},
    {"ZeroMinDiameter", [](TerrainConfig& c) { Craters(c).minDiameter = 0.0; },
     "layers[1].min_diameter"},
    {"MaxBelowMin", [](TerrainConfig& c) { Craters(c).maxDiameter = 5.0; },
     "layers[1].max_diameter"},
    {"InfiniteMax", [](TerrainConfig& c) { Craters(c).maxDiameter = kInfinity; },
     "layers[1].max_diameter"},
    {"TooManyCraters", [](TerrainConfig& c) { Craters(c).minDiameter = 2.0; },
     "layers[1].min_diameter"},
    {"TooManyTraskCraters",
     [](TerrainConfig& c) {
         Craters(c).density = terraingen::TraskSpec{};
         Craters(c).minDiameter = 0.001;
     },
     "layers[1].min_diameter"},
    {"NegativeManualDiameter",
     [](TerrainConfig& c) { Craters(c).manual = {{0.0, 0.0, -1.0, 0.0}}; },
     "layers[1].manual[0].diameter"},
    {"NegativeManualAge", [](TerrainConfig& c) { Craters(c).manual = {{0.0, 0.0, 5.0, -1.0}}; },
     "layers[1].manual[0].age_gyr"},
    {"InfiniteManualPosition",
     [](TerrainConfig& c) { Craters(c).manual = {{kInfinity, 0.0, 5.0, 0.0}}; },
     "layers[1].manual[0]"},
    {"ManualOnlyIgnoresDiameters",
     [](TerrainConfig& c) {
         Craters(c).generate = false;
         Craters(c).minDiameter = -1.0;
     },
     "valid"},
    {"CraterListWithoutPath",
     [](TerrainConfig& c) { c.outputs = {terraingen::CraterListOutput{}}; }, "outputs[0].path"},
    {"BoulderListWithoutPath",
     [](TerrainConfig& c) { c.outputs = {terraingen::BoulderListOutput{}}; }, "outputs[0].path"},
    {"BoulderModelsAsObj",
     [](TerrainConfig& c) { c.outputs = {terraingen::BoulderModelsOutput{"boulders.obj"}}; },
     "outputs[0].path"},
    {"BoulderModelsCreaseTooWide",
     [](TerrainConfig& c) { c.outputs = {terraingen::BoulderModelsOutput{"boulders.glb", 200.0}}; },
     "outputs[0].crease_angle"},
    {"MarsCratersOnLunarSurface",
     [](TerrainConfig& c) { Craters(c).body = terraingen::CraterBody::Mars; }, "layers[1].density"},
    {"LunarCratersOnMarsSurface",
     [](TerrainConfig& c) { Craters(c).density = terraingen::MarsSurfaceSpec{3.0}; },
     "layers[1].density"},
    {"SteadyStateAgesOnMars",
     [](TerrainConfig& c) {
         Craters(c).body = terraingen::CraterBody::Mars;
         Craters(c).density = terraingen::PowerLawSpec{0.1, 2.0};
         Craters(c).ages = terraingen::SteadyStateAgesSpec{};
     },
     "layers[1].ages"},
    {"RoughnessHurstAboveOne",
     [](TerrainConfig& c) { c.layers.push_back(terraingen::RoughnessLayerSpec{{.hurst = 2.0}}); },
     "layers[2]"},
};

class ValidateConfigPaths : public ::testing::TestWithParam<PathCase> {};

}  // namespace

TEST_P(ValidateConfigPaths, ReportsTheProblemByItsPath) {
    EXPECT_EQ(ErrorPath(GetParam().mutate), GetParam().path);
}

INSTANTIATE_TEST_SUITE_P(Small, ValidateConfigPaths, ::testing::ValuesIn(kPathCases),
                         [](const ::testing::TestParamInfo<PathCase>& param) {
                             return std::string(param.param.name);
                         });

TEST(ValidateConfig, AcceptsTheDefaults) {
    EXPECT_NO_THROW(ValidateConfig(TerrainConfig{}));
}

TEST(ValidateConfig, SummarisesTheGridAndTheCratersExpected) {
    TerrainConfig config = Small();
    const terraingen::ConfigSummary summary = ValidateConfig(config);
    EXPECT_EQ(summary.grid.width, 65u);
    EXPECT_EQ(summary.grid.height, 49u);
    ASSERT_TRUE(summary.expectedCraters);
    const auto models = terraingen::BuildCraterModels(Craters(config));
    const double expected = models.density->ExpectedCount(summary.grid.SurfaceArea({}), 8.0, 60.0);
    EXPECT_DOUBLE_EQ(*summary.expectedCraters, expected);

    config.layers.push_back(Craters(config));
    EXPECT_DOUBLE_EQ(*ValidateConfig(config).expectedCraters, 2.0 * expected);
    config.layers.push_back(terraingen::RegionLayerSpec{{}, 1.0, {}, Craters(config)});
    EXPECT_DOUBLE_EQ(*ValidateConfig(config).expectedCraters, 2.0 * expected)
        << "crater layers inside regions are not counted";

    Craters(config).generate = false;
    config.layers.resize(2);
    EXPECT_FALSE(ValidateConfig(config).expectedCraters);
}

TEST(ValidateConfig, ReportsBoulderLayerProblemsByPath) {
    const auto withBoulders = [](auto change) {
        return ErrorPath([&](TerrainConfig& c) {
            terraingen::BoulderLayerSpec boulders;
            boulders.boulders.sizes.minDiameter = 1.0;
            change(boulders.boulders);
            c.layers.push_back(boulders);
        });
    };
    EXPECT_EQ(withBoulders([](terraingen::BoulderLayerOptions&) {}), "valid");
    EXPECT_EQ(withBoulders([](terraingen::BoulderLayerOptions& r) { r.sizes.coverage = 2.0; }),
              "layers[2]");
    EXPECT_EQ(
        withBoulders([](terraingen::BoulderLayerOptions& r) { r.ejecta.minCraterDiameter = 10.0; }),
        "valid");
    EXPECT_EQ(withBoulders([](terraingen::BoulderLayerOptions& r) { r.sizes.minDiameter = 0.01; }),
              "valid");
    EXPECT_EQ(withBoulders([](terraingen::BoulderLayerOptions& r) { r.sizes.maxDiameter = 2.0; }),
              "valid");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) {
                  terraingen::BoulderLayerSpec boulders;
                  boulders.boulders.ejecta.minCraterDiameter = 10.0;
                  c.layers.insert(c.layers.begin() + 1, boulders);
              }),
              "layers[1].ejecta.min_crater_diameter");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) {
                  Flat(c) = {60000, 60000, 0.1};
                  terraingen::BoulderLayerSpec boulders;
                  boulders.boulders.sizes.coverage = 0.5;
                  c.layers = {boulders};
              }),
              "layers[0].min_diameter");
}

TEST(ValidateConfig, ReportsBoulderSizeProblemsByPath) {
    TerrainConfig config = Small();
    Flat(config).pixelSize = 0.5;
    terraingen::BoulderLayerSpec boulders;
    boulders.boulders.sizes.minDiameter = 0.5;
    boulders.boulders.ejecta.minCraterDiameter = 8.0;
    boulders.sizes =
        terraingen::Sp8023IntercraterBouldersSpec{terraingen::SpecificationCurve::Maximum};
    boulders.ejectaSizes = terraingen::Sp8023CraterBouldersSpec{};
    config.layers.push_back(boulders);
    EXPECT_EQ(ValidationErrorPath(config), "layers[2]") << "the crater table starts at 0.85 m";
    std::get<terraingen::BoulderLayerSpec>(config.layers.back()).extrapolate = true;
    EXPECT_EQ(ValidationErrorPath(config), "valid");
    std::get<terraingen::BoulderLayerSpec>(config.layers.back()).sizes =
        terraingen::TabulatedSpec{{1.0}, {0.01}};
    EXPECT_EQ(ValidationErrorPath(config), "layers[2].sizes");
    std::get<terraingen::BoulderLayerSpec>(config.layers.back()).sizes =
        terraingen::GolombekRappBouldersSpec{};
    std::get<terraingen::BoulderLayerSpec>(config.layers.back()).ejectaSizes =
        terraingen::TabulatedSpec{{1.0}, {0.01}};
    EXPECT_EQ(ValidationErrorPath(config), "layers[2].ejecta_sizes");
}

TEST(ValidateConfig, ReportsBedformProblemsByPath) {
    terraingen::BedformOptions ridges = terraingen::MarsTransverseAeolianRidges();
    ridges.leeFraction = 2.0;
    EXPECT_EQ(ErrorPath([&](TerrainConfig& c) {
                  c.layers.push_back(terraingen::BedformLayerSpec{ridges});
              }),
              "layers[2]");
}

TEST(ValidateConfig, ReportsTransformAndRegionProblemsByPath) {
    using terraingen::ClampLayerSpec;
    using terraingen::RegionLayerSpec;
    const auto with = [](terraingen::LayerSpec layer) {
        return ErrorPath([&](TerrainConfig& c) { c.layers.push_back(layer); });
    };
    EXPECT_EQ(with(terraingen::ScaleLayerSpec{2.0, 1.0}), "valid");
    EXPECT_EQ(with(ClampLayerSpec{2.0, 1.0}), "layers[2]");
    EXPECT_EQ(with(RegionLayerSpec{terraingen::CircleRegionSpec{0.0, 0.0, -1.0}}),
              "layers[2].shape");
    EXPECT_EQ(with(RegionLayerSpec{terraingen::RectangleRegionSpec{1.0, 0.0, 0.0, 1.0}}),
              "layers[2].shape");
    EXPECT_EQ(with(RegionLayerSpec{{}, -1.0}), "layers[2].feather");
    EXPECT_EQ(with(RegionLayerSpec{{}, 1.0, {}, ClampLayerSpec{2.0, 1.0}}), "layers[2].layer");
    TerrainConfig small = Small();
    EXPECT_EQ(with(RegionLayerSpec{{}, 1.0, {}, RegionLayerSpec{{}, 1.0, {}, Craters(small)}}),
              "valid");
    terraingen::BoulderLayerSpec boulders;
    boulders.boulders.ejecta.minCraterDiameter = 10.0;
    EXPECT_EQ(ErrorPath([&](TerrainConfig& c) {
                  c.layers = {RegionLayerSpec{{}, 1.0, {}, Craters(c)}, boulders};
              }),
              "layers[1].ejecta.min_crater_diameter")
        << "crater layers inside regions do not count for ejecta";
    EXPECT_EQ(ErrorPath([&](TerrainConfig& c) {
                  c.layers.push_back(RegionLayerSpec{{}, 1.0, {}, boulders});
              }),
              "valid")
        << "boulders inside a region see the crater layers before it";
}

TEST(ValidateConfig, ReportsDiffusionProblemsByPath) {
    const auto error = [](auto mutate) {
        return ErrorPath([&](TerrainConfig& c) {
            terraingen::DiffusionDegradationSpec diffusion;
            mutate(diffusion, Craters(c));
            Craters(c).degradation = diffusion;
        });
    };
    EXPECT_EQ(error([](auto&, auto&) {}), "valid");
    EXPECT_EQ(error([](auto& d, auto&) { d.reachRadii = 1.0; }), "layers[1].degradation");
    EXPECT_EQ(error([](auto& d, auto&) {
                  d.diffusivity = terraingen::DiffusivityModel::PowerLaw;
                  d.powerLaw = {0.0, 1.0, 0.0};
              }),
              "layers[1].degradation");
    EXPECT_EQ(error([](auto&, auto& c) {
                  c.body = terraingen::CraterBody::Mars;
                  c.density = terraingen::MarsSurfaceSpec{};
                  c.ages = terraingen::UniformAgesSpec{};
              }),
              "layers[1].degradation.diffusivity");
    EXPECT_EQ(error([](auto& d, auto& c) {
                  c.body = terraingen::CraterBody::Mars;
                  c.density = terraingen::PowerLawSpec{};
                  c.ages = terraingen::UniformAgesSpec{};
                  d.diffusivity = terraingen::DiffusivityModel::PowerLaw;
              }),
              "valid");
}

TEST(ValidateConfig, ReportsSecondaryLayerProblemsByPath) {
    const auto error = [](auto mutate) {
        return ErrorPath([&](TerrainConfig& c) {
            terraingen::SecondaryLayerSpec layer;
            layer.secondaries.onsetCount = 5.0;
            mutate(c, layer);
            c.layers.push_back(layer);
        });
    };
    EXPECT_EQ(error([](auto&, auto&) {}), "valid");
    EXPECT_EQ(error([](auto&, auto& l) { l.secondaries.onsetCount.reset(); }),
              "layers[2].onset_count");
    EXPECT_EQ(error([](auto&, auto& l) { l.secondaries.sizeSlope = 0.0; }), "layers[2]");
    EXPECT_EQ(error([](auto&, auto& l) { l.depthRatio = 0.0; }), "layers[2].depth_ratio");
    EXPECT_EQ(error([](auto& c, auto&) { c.layers.pop_back(); }), "layers[1]");
}

TEST(ValidateConfig, ReportsRasterSourceProblems) {
    using terraingen::testing::RasterConfig;
    using terraingen::testing::RasterSpec;
    using terraingen::testing::WriteDem;
    const auto folder = TempPath("terraingen_raster_errors");
    WriteDem(folder, "dem.tif");
    WriteDem(folder, "degrees.tif", terraingen::testing::Keys(true));
    const auto error = [&](auto mutate) {
        TerrainConfig config = RasterConfig("dem.tif");
        mutate(config);
        try {
            ValidateConfig(config, folder);
        } catch (const ConfigError& e) {
            return std::string(e.Path()) + ": " + e.what();
        }
        return std::string("valid");
    };
    EXPECT_EQ(error([](TerrainConfig&) {}), "valid");
    EXPECT_EQ(error([](TerrainConfig& c) { RasterSpec(c).path.clear(); }).rfind("source.path", 0),
              0u);
    EXPECT_NE(
        error([](TerrainConfig& c) { RasterSpec(c).path = "missing.tif"; }).find("cannot open"),
        std::string::npos);
    EXPECT_NE(error([](TerrainConfig& c) { RasterSpec(c).band = 1; }).find("source.band"),
              std::string::npos);
    EXPECT_NE(
        error([](TerrainConfig& c) { RasterSpec(c).path = "degrees.tif"; }).find("geographic"),
        std::string::npos);
    EXPECT_NE(error([](TerrainConfig& c) {
                  c.surface = terraingen::SphereSurfaceSpec{1737400.0};
              }).find("source.path: on a sphere the raster must be in longitude and latitude"),
              std::string::npos);
    EXPECT_EQ(error([](TerrainConfig& c) {
                  RasterSpec(c).pixelSize = -1.0;
              }).rfind("source.pixel_size", 0),
              0u);
    EXPECT_EQ(error([](TerrainConfig& c) {
                  RasterSpec(c).pixelSize = 1e-6;
              }).rfind("source.pixel_size", 0),
              0u);
    EXPECT_EQ(error([](TerrainConfig& c) { RasterSpec(c).pixelSize = 1.0; }), "valid");
    EXPECT_EQ(error([](TerrainConfig& c) {
                  terraingen::NoiseLayerSpec noise;
                  noise.stack[0].layer.fractal.octaves = 0;
                  c.layers = {noise};
              }).rfind("layers[0].stack[0]", 0),
              0u);
    EXPECT_EQ(error([](TerrainConfig& c) {
                  CraterLayerSpec craters;
                  craters.minDiameter = 0.0;
                  c.layers = {craters};
              }).rfind("layers[0].min_diameter", 0),
              0u);
    terraingen::Raster<float> rotated(8, 8, 1.0f);
    rotated.SetTransform(terraingen::GeoTransform{0.0, 1.0, 0.2, 0.0, 0.1, -1.0});
    terraingen::WriteGeoTiff(folder / "rotated.tif", rotated, terraingen::testing::Keys());
    EXPECT_EQ(error([](TerrainConfig& c) {
                  RasterSpec(c).path = "rotated.tif";
                  RasterSpec(c).pixelSize = 0.5;
              }).rfind("source.pixel_size", 0),
              0u);
    terraingen::WriteGeoTiff(folder / "tiny.tif", terraingen::Raster<float>(1, 1, 0.0f),
                             terraingen::testing::Keys());
    EXPECT_EQ(
        error([](TerrainConfig& c) { RasterSpec(c).path = "tiny.tif"; }).rfind("source.path", 0),
        0u);
}

TEST(ValidateConfig, ReportsChainProblemsByPath) {
    const auto folder = TempPath("terraingen_chain_errors");
    terraingen::testing::WriteDem(folder, "dem.tif");
    const auto raster = [](const std::string& path) {
        return terraingen::RasterSourceSpec{path, 0, std::nullopt,
                                            terraingen::ResampleMethod::Cubic};
    };
    const auto error = [&](terraingen::ChainSourceSpec chain) {
        TerrainConfig config;
        config.source = std::move(chain);
        return ValidationErrorPath(config, folder);
    };
    EXPECT_EQ(error({{raster("dem.tif"), terraingen::FlatSourceSpec{2, 2, 1.0}}}), "valid");
    EXPECT_EQ(error({}), "source.sources");
    EXPECT_EQ(error({{raster("dem.tif"), raster("missing.tif")}}), "source.sources[1].path");
    EXPECT_EQ(error({{terraingen::ChainSourceSpec{{terraingen::FlatSourceSpec{1, 1, 1.0}}}}}),
              "source.sources[0].sources[0].width");
}

TEST(ValidateConfig, ReportsAlbedoProblemsByPath) {
    using terraingen::testing::WithAlbedo;
    const auto error = [](auto mutate) {
        TerrainConfig config = WithAlbedo();
        mutate(config);
        return ValidationErrorPath(config);
    };
    const auto synthetic = [](TerrainConfig& c) -> terraingen::SyntheticAlbedoSpec& {
        return std::get<terraingen::SyntheticAlbedoSpec>(c.albedo->source);
    };
    const auto craters = [](TerrainConfig& c) -> terraingen::CraterAlbedoSpec& {
        return std::get<terraingen::CraterAlbedoSpec>(c.albedo->layers.at(0));
    };
    EXPECT_EQ(error([](TerrainConfig&) {}), "valid");
    EXPECT_EQ(error([&](TerrainConfig& c) { synthetic(c).value = 2.0; }), "albedo.source");
    EXPECT_EQ(error([&](TerrainConfig& c) {
                  synthetic(c).hueNoise.assign(terraingen::kMaxNoiseLayers + 1, {});
              }),
              "albedo.source.hue_noise");
    EXPECT_EQ(error([&](TerrainConfig& c) {
                  synthetic(c).saturationNoise = {terraingen::NoiseLayerConfig{}};
                  synthetic(c).saturationNoise[0].layer.fractal.octaves = 0;
              }),
              "albedo.source.saturation_noise[0]");
    EXPECT_EQ(error([&](TerrainConfig& c) { synthetic(c).valueNoise[0].hurst = NAN; }),
              "albedo.source.value_noise[0].hurst");
    EXPECT_EQ(error([&](TerrainConfig& c) { craters(c).craterLayer = 1; }),
              "albedo.layers[0].crater_layer");
    EXPECT_EQ(error([&](TerrainConfig& c) { craters(c).options.share = 2.0; }), "albedo.layers[0]");
    EXPECT_EQ(error([](TerrainConfig& c) { c.layers.pop_back(); }), "albedo.layers[0]");
    EXPECT_EQ(error([](TerrainConfig& c) { c.albedo->source = terraingen::RasterAlbedoSpec{}; }),
              "albedo.source.path");
    EXPECT_EQ(error([](TerrainConfig& c) {
                  c.albedo->source = terraingen::RasterAlbedoSpec{"x.tif", {0, 1}, {}, {}, {}};
              }),
              "albedo.source.bands");
    EXPECT_EQ(error([](TerrainConfig& c) {
                  c.albedo->source = terraingen::RasterAlbedoSpec{"missing.tif", {0}, {}, {}, {}};
              }),
              "albedo.source.path");
    EXPECT_EQ(error([](TerrainConfig& c) {
                  c.albedo.reset();
                  c.outputs = {terraingen::AlbedoOutput{"a.tif"}};
              }),
              "outputs[0]");
    EXPECT_EQ(error([](TerrainConfig& c) { c.outputs = {terraingen::AlbedoOutput{}}; }),
              "outputs[0].path");
    EXPECT_EQ(error([](TerrainConfig& c) {
                  c.outputs = {
                      terraingen::AlbedoOutput{"a.tif", terraingen::TiffCompression::PackBits}};
              }),
              "outputs[0].compression");
    EXPECT_EQ(error([](TerrainConfig& c) {
                  c.outputs = {
                      terraingen::AlbedoOutput{"a.tif", terraingen::TiffCompression::Lzw, 10}};
              }),
              "outputs[0].tile_size");
}

TEST(ValidateConfig, ReportsRasterAlbedoProblemsByPath) {
    const auto folder = TempPath("terraingen_albedo_errors");
    terraingen::testing::WriteDem(folder, "dem.tif");
    terraingen::Raster<float> map(10, 8, 1000.0f);
    map.SetTransform(terraingen::GeoTransform::NorthUp(1000.0, 2000.0, 20.0));
    terraingen::WriteGeoTiff(folder / "map.tif", map, terraingen::testing::Keys());
    const auto rejected = [&](terraingen::RasterAlbedoSpec spec) {
        TerrainConfig config = terraingen::testing::RasterConfig("dem.tif");
        config.albedo = terraingen::AlbedoSpec{std::move(spec), {}};
        return ValidationErrorPath(config, folder);
    };
    EXPECT_EQ(rejected({"map.tif", {0}, {}, {}, {}}), "valid");
    EXPECT_EQ(rejected({"map.tif", {1}, {}, {}, {}}), "albedo.source.bands");
    EXPECT_EQ(rejected({"map.tif", {0}, NAN, {}, {}}), "albedo.source.scale");
    EXPECT_EQ(rejected({"map.tif", {0}, {}, NAN, {}}), "albedo.source.scale");
    terraingen::RasterAlbedoSpec detail{"map.tif", {0}, {}, {}, {}};
    detail.options.detail.octaves = 0;
    EXPECT_EQ(rejected(detail), "albedo.source.detail");
    terraingen::Raster<float> rotated = map;
    rotated.SetTransform({1000.0, 20.0, 1.0, 2000.0, 0.0, -20.0});
    terraingen::WriteGeoTiff(folder / "rotated.tif", rotated, terraingen::testing::Keys());
    EXPECT_EQ(rejected({"rotated.tif", {0}, {}, {}, {}}), "albedo.source.path");
}

TEST(ValidateConfig, ReportsSphereProblemsByPath) {
    using terraingen::testing::OnASphere;
    const auto error = [](auto mutate) {
        TerrainConfig config = OnASphere();
        mutate(config);
        return ValidationErrorPath(config);
    };
    EXPECT_EQ(error([](TerrainConfig&) {}), "valid");
    EXPECT_EQ(error([](TerrainConfig& c) { c.surface = terraingen::SphereSurfaceSpec{}; }),
              "surface.radius");
    EXPECT_EQ(error([](TerrainConfig& c) { Flat(c).centreY = 89.999; }), "source.height");
    EXPECT_EQ(error([](TerrainConfig& c) { Flat(c).centreX = NAN; }), "source.centre_x");
    EXPECT_EQ(error([](TerrainConfig& c) {
                  Noise(c).stack[0].layer.fractal.basis = terraingen::NoiseBasis::Simplex;
              }),
              "layers[0].stack[0].basis");
    EXPECT_EQ(
        error([](TerrainConfig& c) { c.outputs = {terraingen::BoulderModelsOutput{"b.glb"}}; }),
        "valid");
    EXPECT_EQ(error([](TerrainConfig& c) {
                  terraingen::SyntheticAlbedoSpec synthetic;
                  synthetic.valueNoise = {terraingen::NoiseLayerConfig{}};
                  synthetic.valueNoise[0].layer.fractal.basis = terraingen::NoiseBasis::WorleyF1;
                  c.albedo = terraingen::AlbedoSpec{synthetic, {}};
              }),
              "albedo.source.value_noise[0].basis");
}

TEST(ValidateConfig, ReportsBodyMeshProblemsByPath) {
    const auto error = [](auto mutate) {
        TerrainConfig config;
        config.surface = terraingen::SphereSurfaceSpec{0.0, terraingen::Body::Mars};
        config.source = terraingen::FlatSourceSpec{4, 4, 1000.0};
        terraingen::BodyMeshOutput output{"m.glb", 8, std::nullopt};
        mutate(config, output);
        config.outputs = {output};
        return ValidationErrorPath(config);
    };
    EXPECT_EQ(error([](auto&, auto&) {}), "valid");
    EXPECT_EQ(error([](auto& c, auto&) { c.surface = terraingen::PlaneSurfaceSpec{}; }),
              "outputs[0]");
    EXPECT_EQ(error([](auto&, auto& o) { o.path = "m.obj"; }), "outputs[0].path");
    EXPECT_EQ(error([](auto&, auto& o) { o.cellsPerFace = 0; }), "outputs[0].cells_per_face");
    EXPECT_EQ(error([](auto&, auto& o) { o.spacing = -1.0; }), "outputs[0].spacing");
}

TEST(DescribeSource, ReadsTheRasterHeaderAndResampledSize) {
    const auto folder = TempPath("terraingen_describe");
    terraingen::testing::WriteDem(folder, "dem.tif");
    TerrainConfig config = terraingen::testing::RasterConfig("dem.tif");
    const auto grid = terraingen::DescribeSource(config.source, folder);
    EXPECT_EQ(grid.width, 40u);
    EXPECT_EQ(grid.height, 30u);
    EXPECT_EQ(grid.transform, terraingen::GeoTransform::NorthUp(1000.0, 2000.0, 5.0));
    EXPECT_DOUBLE_EQ(grid.PixelSize(), 5.0);
    EXPECT_DOUBLE_EQ(grid.Area(), 200.0 * 150.0);
    const terraingen::Bounds2d bounds = grid.Bounds();
    EXPECT_EQ(bounds.minX, 1000.0);
    EXPECT_EQ(bounds.maxX, 1200.0);
    EXPECT_EQ(bounds.minY, 1850.0);
    EXPECT_EQ(bounds.maxY, 2000.0);
    terraingen::testing::RasterSpec(config).pixelSize = 2.5;
    const auto fine = terraingen::DescribeSource(config.source, folder);
    EXPECT_EQ(fine.width, 80u);
    EXPECT_EQ(fine.height, 60u);
    EXPECT_DOUBLE_EQ(fine.PixelSize(), 2.5);
    EXPECT_EQ(fine.transform.pixelHeight, -2.5) << "keeps the north-up sign";
}

TEST(BuildSurface, BuildsPlanesAndSpheresOfARadiusOrABody) {
    EXPECT_EQ(terraingen::BuildSurface(terraingen::PlaneSurfaceSpec{}),
              terraingen::Surface::Plane());
    EXPECT_EQ(terraingen::BuildSurface(terraingen::SphereSurfaceSpec{3389500.0}),
              terraingen::Surface::Sphere(3389500.0));
    EXPECT_EQ(terraingen::BuildSurface(terraingen::SphereSurfaceSpec{0.0, terraingen::Body::Moon}),
              terraingen::Surface::Sphere(1737400.0));
    EXPECT_THROW(terraingen::BuildSurface(terraingen::SphereSurfaceSpec{-1.0}), ConfigError);
    EXPECT_THROW(
        terraingen::BuildSurface(terraingen::SphereSurfaceSpec{5.0, terraingen::Body::Moon}),
        ConfigError);
}
