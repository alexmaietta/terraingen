#include "configs.h"
#include "terraingen/config/generate.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

using terraingen::TerrainConfig;
using terraingen::ValidateConfig;
using terraingen::testing::Craters;
using terraingen::testing::ErrorPath;
using terraingen::testing::Noise;
using terraingen::testing::Region;
using terraingen::testing::Small;
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
    {"NarrowGrid", [](TerrainConfig& c) { Region(c).width = 1; }, "outputs[0].region.width"},
    {"TallGrid", [](TerrainConfig& c) { Region(c).height = 100000; }, "outputs[0].region.width"},
    {"ZeroPixelSize", [](TerrainConfig& c) { Region(c).pixelSize = 0.0; },
     "outputs[0].region.pixel_size"},
    {"EmptyNoiseStack", [](TerrainConfig& c) { Noise(c).stack.clear(); }, "layers[0].stack"},
    {"NoOctaves", [](TerrainConfig& c) { Noise(c).stack[0].layer.fractal.octaves = 0; },
     "layers[0].stack[0]"},
    {"InfiniteHurst", [](TerrainConfig& c) { Noise(c).stack[0].hurst = 1.0f / 0.0f; },
     "layers[0].stack[0].hurst"},
    {"HeightmapWithoutPath",
     [](TerrainConfig& c) { c.outputs = {terraingen::testing::Heightmap("")}; }, "outputs[0].path"},
    {"PackBitsHeightmap",
     [](TerrainConfig& c) {
         c.outputs = {terraingen::HeightmapOutput{"a.tif", terraingen::TiffCompression::PackBits,
                                                  256, terraingen::testing::SmallRegion()}};
     },
     "outputs[0].compression"},
    {"OddTileSize",
     [](TerrainConfig& c) {
         c.outputs = {terraingen::HeightmapOutput{"a.tif", terraingen::TiffCompression::Lzw, 20,
                                                  terraingen::testing::SmallRegion()}};
     },
     "outputs[0].tile_size"},
    {"LunarSurfaceTooOld",
     [](TerrainConfig& c) { Craters(c).density = terraingen::testing::LunarDensitySpec(9.0); },
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
    {"NegativeRimStrength", [](TerrainConfig& c) { Craters(c).apply.rimSmoothing.strength = -1.0; },
     "layers[1].apply"},
    {"ZeroNoiseOctaves", [](TerrainConfig& c) { Craters(c).apply.noise.octaves = 0; },
     "layers[1].apply"},
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
     [](TerrainConfig& c) { c.outputs = {terraingen::BoulderModelsOutput{"boulders.fbx"}}; },
     "outputs[0].path"},
    {"BoulderModelsCreaseTooWide",
     [](TerrainConfig& c) { c.outputs = {terraingen::BoulderModelsOutput{"boulders.glb", 200.0}}; },
     "outputs[0].crease_angle"},
    {"EmptyMinimumDensity",
     [](TerrainConfig& c) { Craters(c).density = terraingen::MinimumSpec{}; }, "layers[1].density"},
    {"DunesSteeperThanASlipFace",
     [](TerrainConfig& c) {
         terraingen::DuneLayerSpec dunes{terraingen::MarsBarchans()};
         dunes.dunes.stossSlope = dunes.dunes.slipFaceAngle;
         c.layers.push_back(dunes);
     },
     "layers[2]"},
    {"MarsDunesWithRipples",
     [](TerrainConfig& c) {
         c.layers.push_back(terraingen::DuneLayerSpec{terraingen::MarsBarchans()});
     },
     "valid"},
    {"MarsMorphometryOnALunarSurface",
     [](TerrainConfig& c) { Craters(c).morphometry = terraingen::CraterMorphometryFit::Mars; },
     "valid"},
    {"IvanovChronologyTooOld",
     [](TerrainConfig& c) {
         Craters(c).density = terraingen::ProductionSurfaceSpec{
             terraingen::ProductionFunction::Ivanov, terraingen::ChronologyModel::Ivanov, 9.0};
     },
     "layers[1].density"},
    {"SteadyStateAgesTooOld",
     [](TerrainConfig& c) { Craters(c).ages = terraingen::testing::LunarAgesSpec(9.0); },
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

TEST(ValidateConfig, SummarisesTheGridsAndTheCratersExpected) {
    TerrainConfig config = Small();
    const terraingen::ConfigSummary summary = ValidateConfig(config);
    EXPECT_FALSE(summary.base);
    ASSERT_EQ(summary.outputGrids.size(), 1u);
    const terraingen::RasterGrid grid = summary.outputGrids[0].value();
    EXPECT_EQ(grid.width, 65u);
    EXPECT_EQ(grid.height, 49u);
    ASSERT_TRUE(summary.expectedCraters);
    const auto models = terraingen::BuildCraterModels(Craters(config));
    const double expected = models.density->ExpectedCount(grid.SurfaceArea({}), 8.0, 60.0);
    EXPECT_DOUBLE_EQ(*summary.expectedCraters, expected);

    TerrainConfig larger = config;
    larger.outputs.push_back(terraingen::testing::Heightmap("big.tif", {{130, 98, 2.0}}));
    EXPECT_DOUBLE_EQ(*ValidateConfig(larger).expectedCraters,
                     models.density->ExpectedCount(4.0 * grid.SurfaceArea({}), 8.0, 60.0))
        << "the output grid with the most";

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
                  Region(c) = {60000, 60000, 0.1};
                  terraingen::BoulderLayerSpec boulders;
                  boulders.boulders.sizes.coverage = 0.5;
                  c.layers = {boulders};
              }),
              "layers[0].min_diameter");
}

TEST(ValidateConfig, ReportsBoulderSizeProblemsByPath) {
    TerrainConfig config = Small();
    Region(config).pixelSize = 0.5;
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
    EXPECT_EQ(error([](auto& d, auto& c) {
                  c.morphometry = terraingen::CraterMorphometryFit::Mars;
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
