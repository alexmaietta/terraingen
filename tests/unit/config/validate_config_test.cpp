#include "configs.h"
#include "terraingen/config/generate.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

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
    {"TallGrid", [](TerrainConfig& c) { Region(c).height = 100000; }, "outputs[0].region.height"},
    {"ZeroPixelSize", [](TerrainConfig& c) { Region(c).pixelSize = 0.0; },
     "outputs[0].region.pixel_size"},
    {"EmptyNoiseStack", [](TerrainConfig& c) { Noise(c).stack.clear(); }, "layers[0].stack"},
    {"NoOctaves", [](TerrainConfig& c) { Noise(c).stack[0].layer.fractal.octaves = 0; },
     "layers[0].stack[0].octaves"},
    {"InfiniteHurst",
     [](TerrainConfig& c) { Noise(c).stack[0].hurst = std::numeric_limits<float>::infinity(); },
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
     "layers[1].density.distributions[0].age_gyr"},
    {"NoLifetime", [](TerrainConfig& c) { Craters(c).ages = terraingen::UniformAgesSpec{0.0}; },
     "layers[1].ages.lifetime_gyr"},
    {"NoFreshnessLifetime",
     [](TerrainConfig& c) { Craters(c).ages = terraingen::FreshnessAgesSpec{0.0}; },
     "layers[1].ages.lifetime_gyr"},
    {"ReversedFreshness",
     [](TerrainConfig& c) {
         Craters(c).ages = terraingen::FreshnessAgesSpec{
             1.0, {terraingen::ValueDistribution::Kind::Uniform, 0.6, 0.2}};
     },
     "layers[1].ages"},
    {"NegativeFreshnessWeight",
     [](TerrainConfig& c) {
         terraingen::FreshnessAgesSpec ages;
         ages.freshness = {terraingen::ValueDistribution::Kind::Piecewise};
         ages.freshness.edges = {0.0, 1.0};
         ages.freshness.weights = {-1.0};
         Craters(c).ages = ages;
     },
     "layers[1].ages.freshness.weights[0]"},
    {"DeepCutoff",
     [](TerrainConfig& c) { Craters(c).degradation = terraingen::FreshnessDegradationSpec{0.5}; },
     "layers[1].degradation.cutoff_depth_ratio"},
    {"SpacingRatioBelowOne", [](TerrainConfig& c) { Craters(c).spacing.diameterRatio = 0.5; },
     "layers[1].spacing.diameter_ratio"},
    {"ReplacementAboveOne", [](TerrainConfig& c) { Craters(c).apply.interiorReplacement = 2.0; },
     "layers[1].apply.interior_replacement"},
    {"NegativeRimStrength", [](TerrainConfig& c) { Craters(c).apply.rimSmoothing.strength = -1.0; },
     "layers[1].apply.rim_smoothing.strength"},
    {"ZeroNoiseOctaves", [](TerrainConfig& c) { Craters(c).apply.noise.octaves = 0; },
     "layers[1].apply.noise.octaves"},
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
    {"ManualDepth",
     [](TerrainConfig& c) { Craters(c).manual = {{0.0, 0.0, 6.0, 0.0, 0u, false, 0.9, 0.2}}; },
     "valid"},
    {"ZeroManualDepth",
     [](TerrainConfig& c) { Craters(c).manual = {{0.0, 0.0, 6.0, 0.0, 0u, false, 0.0}}; },
     "layers[1].manual[0].depth"},
    {"NegativeManualRimHeight",
     [](TerrainConfig& c) { Craters(c).manual = {{0.0, 0.0, 6.0, 0.0, 0u, false, 0.9, -0.1}}; },
     "layers[1].manual[0].rim_height"},
    {"ManualRimHeightWithoutDepth",
     [](TerrainConfig& c) {
         Craters(c).manual = {{0.0, 0.0, 6.0, 0.0, 0u, false, std::nan(""), 0.2}};
     },
     "layers[1].manual[0].rim_height"},
    {"ManualRimHeightTheProfileFixes",
     [](TerrainConfig& c) {
         Craters(c).profile = terraingen::FassettThomsonSpec{};
         Craters(c).manual = {{0.0, 0.0, 6.0, 0.0, 0u, false, 0.9, 0.2}};
     },
     "layers[1].manual[0]"},
    {"ManualOnlyStillChecksDiameterRanges",
     [](TerrainConfig& c) {
         Craters(c).generate = false;
         Craters(c).minDiameter = -1.0;
     },
     "layers[1].min_diameter"},
    {"CraterListWithoutPath",
     [](TerrainConfig& c) { c.outputs = {terraingen::CraterListOutput{}}; }, "outputs[0].path"},
    {"BoulderListWithoutPath",
     [](TerrainConfig& c) { c.outputs = {terraingen::BoulderListOutput{}}; }, "outputs[0].path"},
    {"BoulderModelsAsObj",
     [](TerrainConfig& c) { c.outputs = {terraingen::BoulderModelsOutput{"boulders.fbx"}}; },
     "outputs[0].path"},
    {"BoulderModelsCreaseTooWide",
     [](TerrainConfig& c) { c.outputs = {terraingen::BoulderModelsOutput{"boulders.glb", 200.0}}; },
     "outputs[0].crease_angle_deg"},
    {"EmptyMinimumDensity",
     [](TerrainConfig& c) { Craters(c).density = terraingen::MinimumSpec{}; }, "layers[1].density"},
    {"PavementWithoutOctaves",
     [](TerrainConfig& c) {
         terraingen::PavementLayerSpec pavement;
         pavement.pavement.octaves = 0;
         c.layers.push_back(pavement);
     },
     "layers[2].octaves"},
    {"PhotometryNeedsBedrockUnderPavement",
     [](TerrainConfig& c) {
         c.materials = terraingen::MaterialsSpec{};
         c.materials->options.exposure.reset();
         c.materials->photometry.classes[0] = terraingen::HapkeParameters{0.7, 0.3, 0.5, 10.0};
         c.layers.push_back(terraingen::PavementLayerSpec{});
         c.outputs.push_back(terraingen::PhotometryOutput{
             {"p.tif", terraingen::TiffCompression::Lzw, 256, terraingen::testing::SmallRegion()}});
     },
     "materials.photometry.bedrock"},
    {"StrataBelowSharpnessOne",
     [](TerrainConfig& c) {
         terraingen::StrataLayerSpec strata;
         strata.strata.sharpness = 0.5;
         c.layers.push_back(strata);
     },
     "layers[2].sharpness"},
    {"PhotometryNeedsBedrockUnderStrata",
     [](TerrainConfig& c) {
         c.materials = terraingen::MaterialsSpec{};
         c.materials->options.exposure.reset();
         c.materials->photometry.classes[0] = terraingen::HapkeParameters{0.7, 0.3, 0.5, 10.0};
         c.layers.push_back(terraingen::StrataLayerSpec{});
         c.outputs.push_back(terraingen::PhotometryOutput{
             {"p.tif", terraingen::TiffCompression::Lzw, 256, terraingen::testing::SmallRegion()}});
     },
     "materials.photometry.bedrock"},
    {"SandShadowWithoutBoulders",
     [](TerrainConfig& c) { c.layers.push_back(terraingen::SandShadowLayerSpec{}); }, "layers[2]"},
    {"SandShadowWithoutLength",
     [](TerrainConfig& c) {
         c.layers.push_back(terraingen::BoulderLayerSpec{});
         terraingen::SandShadowLayerSpec shadows;
         shadows.shadows.lengthRatio = 0.0;
         c.layers.push_back(shadows);
     },
     "layers[3].length_ratio"},
    {"PhotometryNeedsRockUnderBoulders",
     [](TerrainConfig& c) {
         c.materials = terraingen::MaterialsSpec{};
         c.materials->options.exposure.reset();
         c.materials->photometry.classes[0] = terraingen::HapkeParameters{0.7, 0.3, 0.5, 10.0};
         c.layers.push_back(terraingen::BoulderLayerSpec{});
         c.outputs.push_back(terraingen::PhotometryOutput{
             {"p.tif", terraingen::TiffCompression::Lzw, 256, terraingen::testing::SmallRegion()}});
     },
     "materials.photometry.rock"},
    {"VentifactAnglesOutOfOrder",
     [](TerrainConfig& c) {
         terraingen::BoulderLayerSpec boulders;
         boulders.boulders.ventifacts.share = 0.5;
         boulders.boulders.ventifacts.minAngle = 1.2;
         boulders.boulders.ventifacts.maxAngle = 1.0;
         c.layers.push_back(boulders);
     },
     "layers[2]"},
    {"MidpointDisplacementWithoutBase",
     [](TerrainConfig& c) { c.layers.push_back(terraingen::MidpointDisplacementLayerSpec{}); },
     "layers[2]"},
    {"MidpointDisplacementDimensionAboveThree",
     [](TerrainConfig& c) {
         terraingen::MidpointDisplacementLayerSpec layer;
         layer.fractalDimension = 3.5;
         c.layers.push_back(layer);
     },
     "layers[2].fractal_dimension"},
    {"SandFillWithoutReach",
     [](TerrainConfig& c) { c.layers.push_back(terraingen::SandFillLayerSpec{{0.0}}); },
     "layers[2].reach"},
    {"MaterialsOutputWithoutMaterials",
     [](TerrainConfig& c) {
         c.outputs.push_back(terraingen::MaterialsOutput{
             {"m.tif", terraingen::TiffCompression::Lzw, 256, terraingen::testing::SmallRegion()}});
     },
     "outputs[1]"},
    {"PhotometryOutputWithoutMaterials",
     [](TerrainConfig& c) {
         c.outputs.push_back(terraingen::PhotometryOutput{
             {"p.tif", terraingen::TiffCompression::Lzw, 256, terraingen::testing::SmallRegion()}});
     },
     "outputs[1]"},
    {"TooMuchDust",
     [](TerrainConfig& c) {
         c.materials = terraingen::MaterialsSpec{};
         c.materials->options.dustCover = 1.0;
     },
     "materials.dust_cover"},
    {"PhotometryNeedsBedrockWithExposure",
     [](TerrainConfig& c) {
         c.materials = terraingen::MaterialsSpec{};
         c.materials->photometry.classes[0] = terraingen::HapkeParameters{0.7, 0.3, 0.5, 10.0};
         c.outputs.push_back(terraingen::PhotometryOutput{
             {"p.tif", terraingen::TiffCompression::Lzw, 256, terraingen::testing::SmallRegion()}});
     },
     "materials.photometry.bedrock"},
    {"MarsPancamCoversBedrock",
     [](TerrainConfig& c) {
         c.materials = terraingen::MaterialsSpec{};
         c.materials->photometry.reference = terraingen::PhotometryReference::MarsPancam;
         c.outputs.push_back(terraingen::PhotometryOutput{
             {"p.tif", terraingen::TiffCompression::Lzw, 256, terraingen::testing::SmallRegion()}});
     },
     "valid"},
    {"PhotometryNeedsSoilWithoutAReference",
     [](TerrainConfig& c) {
         c.materials = terraingen::MaterialsSpec{};
         c.materials->options.exposure.reset();
         c.outputs.push_back(terraingen::PhotometryOutput{
             {"p.tif", terraingen::TiffCompression::Lzw, 256, terraingen::testing::SmallRegion()}});
     },
     "materials.photometry.soil"},
    {"PhotometryNeedsSandUnderDunes",
     [](TerrainConfig& c) {
         c.materials = terraingen::MaterialsSpec{};
         c.materials->options.exposure.reset();
         c.materials->photometry.classes[0] = terraingen::HapkeParameters{0.7, 0.3, 0.5, 10.0};
         c.layers.push_back(terraingen::DuneLayerSpec{});
         c.outputs.push_back(terraingen::PhotometryOutput{
             {"p.tif", terraingen::TiffCompression::Lzw, 256, terraingen::testing::SmallRegion()}});
     },
     "materials.photometry.sand"},
    {"PhotometryWithBedrockGiven",
     [](TerrainConfig& c) {
         c.materials = terraingen::MaterialsSpec{};
         c.materials->photometry.reference = terraingen::PhotometryReference::MarsPancam;
         c.materials->photometry.classes[3] = terraingen::HapkeParameters{0.7, 0.4, 0.3, 15.0};
         c.materials->options.dustCover = 0.2;
         c.outputs.push_back(terraingen::PhotometryOutput{
             {"p.tif", terraingen::TiffCompression::Lzw, 256, terraingen::testing::SmallRegion()}});
     },
     "valid"},
    {"OutOfRangeHapke",
     [](TerrainConfig& c) {
         c.materials = terraingen::MaterialsSpec{};
         c.materials->photometry.classes[2] = terraingen::HapkeParameters{1.5, 0.3, 0.5, 10.0};
     },
     "materials.photometry.dust.albedo"},
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
     "layers[1].density.age_gyr"},
    {"SteadyStateAgesTooOld",
     [](TerrainConfig& c) { Craters(c).ages = terraingen::testing::LunarAgesSpec(9.0); },
     "layers[1].ages.age_gyr"},
    {"RoughnessHurstAboveOne",
     [](TerrainConfig& c) {
         terraingen::RoughnessOptions roughness;
         roughness.hurst = 2.0;
         c.layers.push_back(terraingen::RoughnessLayerSpec{roughness});
     },
     "layers[2].hurst"},
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

TEST(ValidateConfig, SaysWhyAManualCratersShapeIsRejected) {
    const auto message = [](void (*mutate)(TerrainConfig&)) {
        TerrainConfig config = Small();
        mutate(config);
        try {
            ValidateConfig(config);
        } catch (const terraingen::ConfigError& error) {
            return std::string(error.what());
        }
        return std::string("valid");
    };
    EXPECT_NE(message([](TerrainConfig& c) {
                  Craters(c).manual = {{0.0, 0.0, 6.0, 0.0, 0u, false, std::nan(""), 0.2}};
              }).find("needs a depth"),
              std::string::npos);
    const std::string shallow = message([](TerrainConfig& c) {
        Craters(c).degradation = terraingen::DiffusionDegradationSpec{};
        Craters(c).manual = {{0.0, 0.0, 100.0, 0.0, 0u, false, 0.01}};
    });
    EXPECT_NE(shallow.find("shallower than diffusion wears it"), std::string::npos) << shallow;
}

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
    config.layers.push_back(terraingen::RegionLayerSpec{
        terraingen::CircleRegionSpec{0.0, 0.0, 10.0}, 1.0, {}, Craters(config)});
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
              "layers[2].coverage");
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
                  boulders.sizes = terraingen::GolombekRappBouldersSpec{};
                  boulders.boulders.sizes.coverage = 0.5;
                  c.layers = {boulders};
              }),
              "layers[0].min_diameter");
}

TEST(ValidateConfig, ReportsManualBoulderProblemsByPath) {
    const auto withManual = [](auto change) {
        return ErrorPath([&](TerrainConfig& c) {
            terraingen::BoulderLayerSpec boulders;
            boulders.boulders.sizes.minDiameter = 1.0;
            boulders.manual = {{1.0, 2.0, 1.5}, {-3.0, 4.0, 2.0}};
            change(boulders.manual);
            c.layers.push_back(boulders);
        });
    };
    using Manual = std::vector<terraingen::ManualBoulder>;
    EXPECT_EQ(withManual([](Manual&) {}), "valid");
    EXPECT_EQ(withManual([](Manual& m) {
                  m[1].burial = 0.5;
                  m[1].tilt = 0.2;
                  m[1].rest = terraingen::BoulderRest::End;
              }),
              "valid");
    EXPECT_EQ(withManual([](Manual& m) { m[1].burial = 1.0; }), "layers[2].manual[1].burial");
    EXPECT_EQ(withManual([](Manual& m) { m[0].height = 0.0; }), "layers[2].manual[0].height");
    EXPECT_EQ(withManual([](Manual& m) { m[0].wear = 1.5; }), "layers[2].manual[0].wear");
    EXPECT_EQ(withManual([](Manual& m) { m[0].diameter = -1.0; }), "layers[2].manual[0].diameter");
    EXPECT_EQ(withManual([](Manual& m) { m[1].yaw = kInfinity; }), "layers[2].manual[1]");
    EXPECT_EQ(withManual([](Manual& m) { m[1] = m[0]; }), "layers[2].manual[1]")
        << "two boulders with one ID";
    EXPECT_EQ(withManual([](Manual& m) {
                  m[0].id = 5;
                  m[1].id = 5;
              }),
              "layers[2].manual[1]");
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

TEST(ValidateConfig, EjectaSizesFollowTheSizesWhenUnset) {
    TerrainConfig config = Small();
    terraingen::BoulderLayerSpec boulders;
    boulders.boulders.sizes.minDiameter = 0.5;
    boulders.boulders.ejecta.minCraterDiameter = 8.0;
    boulders.sizes = terraingen::PowerLawSpec{6.9e-4, 2.56};
    config.layers.push_back(boulders);
    EXPECT_EQ(ValidationErrorPath(config), "valid");
    const auto options = terraingen::config::BuildBoulderOptions(boulders, "layers[2]");
    ASSERT_NE(options.ejecta.distribution, nullptr);
    EXPECT_DOUBLE_EQ(options.ejecta.distribution->Cumulative(1.0), 6.9e-4);
}

TEST(ValidateConfig, RejectsGolombekRappTermsThatNoSizesUse) {
    const auto errorPath = [](auto change) {
        TerrainConfig config = Small();
        terraingen::BoulderLayerSpec boulders;
        boulders.boulders.sizes.minDiameter = 0.5;
        change(boulders);
        config.layers.push_back(boulders);
        return ValidationErrorPath(config);
    };
    using Spec = terraingen::BoulderLayerSpec;
    const auto coverage = [](Spec& s) { s.boulders.sizes.coverage = 0.05; };
    EXPECT_EQ(errorPath(coverage), "layers[2].coverage") << "the default sizes are SP-8023";
    EXPECT_EQ(errorPath([&](Spec& s) {
                  coverage(s);
                  s.sizes = terraingen::GolombekRappBouldersSpec{};
              }),
              "valid");
    EXPECT_EQ(errorPath([](Spec& s) { s.boulders.sizes.qConstant = 2.0; }), "layers[2].qa");
    EXPECT_EQ(errorPath([](Spec& s) { s.boulders.sizes.qCoverageCoefficient = 0.2; }),
              "layers[2].qb");
    EXPECT_EQ(errorPath([](Spec& s) {
                  s.boulders.sizes.qConstant = 2.0;
                  s.ejectaSizes = terraingen::GolombekRappBouldersSpec{};
              }),
              "valid")
        << "Golombek-Rapp ejecta use qa";

    const auto ejecta = [](Spec& s) { s.boulders.ejecta.coverage = 0.2; };
    EXPECT_EQ(errorPath(ejecta), "layers[2].ejecta.coverage");
    EXPECT_EQ(errorPath([&](Spec& s) {
                  ejecta(s);
                  s.sizes = terraingen::GolombekRappBouldersSpec{};
              }),
              "valid")
        << "ejecta sizes follow the sizes";
    EXPECT_EQ(errorPath([&](Spec& s) {
                  ejecta(s);
                  s.sizes = terraingen::GolombekRappBouldersSpec{};
                  s.ejectaSizes = terraingen::Sp8023CraterBouldersSpec{};
              }),
              "layers[2].ejecta.coverage");
}

TEST(ValidateConfig, NamesGolombekRappSizesForACoverage) {
    const TerrainConfig config = terraingen::ParseConfig(R"({"schema_version": 1,
        "layers": [{"type": "boulders", "coverage": 0.05}],
        "outputs": [{"type": "heightmap", "path": "a.tif",
                     "region": {"width": 33, "height": 33, "pixel_size": 1}}]})");
    try {
        ValidateConfig(config);
        ADD_FAILURE() << "validated";
    } catch (const terraingen::ConfigError& error) {
        EXPECT_EQ(error.Path(), "layers[0].coverage");
        EXPECT_NE(std::string(error.what()).find(R"("sizes": {"type": "golombek_rapp"})"),
                  std::string::npos)
            << error.what();
    }
}

TEST(ValidateConfig, RejectsSteadyStateAgesBelowTheProductionFunction) {
    TerrainConfig config = Small();
    Craters(config).ages = terraingen::SteadyStateAgesSpec{};
    Craters(config).minDiameter = 1.0;
    try {
        terraingen::ValidateConfig(config);
        ADD_FAILURE() << "validated";
    } catch (const terraingen::ConfigError& error) {
        EXPECT_EQ(error.Path(), "layers[1].min_diameter");
        EXPECT_NE(std::string(error.what()).find("2.5 m"), std::string::npos) << error.what();
    }
    Craters(config).minDiameter = 2.5;
    EXPECT_EQ(ValidationErrorPath(config), "valid");
}

TEST(ValidateConfig, ReportsBedformProblemsByPath) {
    terraingen::BedformOptions ridges = terraingen::MarsTransverseAeolianRidges();
    ridges.leeFraction = 2.0;
    EXPECT_EQ(ErrorPath([&](TerrainConfig& c) {
                  c.layers.push_back(terraingen::BedformLayerSpec{ridges});
              }),
              "layers[2].lee_fraction");
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
              "layers[2].shape.radius");
    EXPECT_EQ(with(RegionLayerSpec{terraingen::RectangleRegionSpec{1.0, 0.0, 0.0, 1.0}}),
              "layers[2].shape");
    EXPECT_EQ(with(RegionLayerSpec{}), "layers[2].shape") << "the default shape has no area";
    EXPECT_EQ(with(RegionLayerSpec{terraingen::CircleRegionSpec{0.0, 0.0, 0.0}, 5.0}),
              "layers[2].shape");
    EXPECT_EQ(with(RegionLayerSpec{terraingen::RectangleRegionSpec{0.0, 0.0, 4.0, 4.0}}), "valid");
    EXPECT_EQ(with(RegionLayerSpec{terraingen::CircleRegionSpec{0.0, 0.0, 10.0}, -1.0}),
              "layers[2].feather");
    EXPECT_EQ(with(RegionLayerSpec{
                  terraingen::CircleRegionSpec{0.0, 0.0, 10.0}, 1.0, {}, ClampLayerSpec{2.0, 1.0}}),
              "layers[2].layer");
    TerrainConfig small = Small();
    EXPECT_EQ(with(RegionLayerSpec{
                  terraingen::CircleRegionSpec{0.0, 0.0, 10.0},
                  1.0,
                  {},
                  RegionLayerSpec{
                      terraingen::CircleRegionSpec{0.0, 0.0, 10.0}, 1.0, {}, Craters(small)}}),
              "valid");
    terraingen::BoulderLayerSpec boulders;
    boulders.boulders.ejecta.minCraterDiameter = 10.0;
    EXPECT_EQ(
        ErrorPath([&](TerrainConfig& c) {
            c.layers = {
                RegionLayerSpec{terraingen::CircleRegionSpec{0.0, 0.0, 10.0}, 1.0, {}, Craters(c)},
                boulders};
        }),
        "layers[1].ejecta.min_crater_diameter")
        << "crater layers inside regions do not count for ejecta";
    EXPECT_EQ(ErrorPath([&](TerrainConfig& c) {
                  c.layers.push_back(RegionLayerSpec{
                      terraingen::CircleRegionSpec{0.0, 0.0, 10.0}, 1.0, {}, boulders});
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
              "layers[1].degradation.power_law.coefficient");
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
    EXPECT_EQ(error([](auto&, auto& l) { l.secondaries.sizeSlope = 0.0; }), "layers[2].size_slope");
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
    EXPECT_EQ(error([&](TerrainConfig& c) { synthetic(c).value = 2.0; }), "albedo.source.value");
    EXPECT_EQ(error([&](TerrainConfig& c) {
                  synthetic(c).hueNoise.assign(terraingen::kMaxNoiseLayers + 1, {});
              }),
              "albedo.source.hue_noise_deg");
    EXPECT_EQ(error([&](TerrainConfig& c) {
                  synthetic(c).saturationNoise = {terraingen::NoiseLayerConfig{}};
                  synthetic(c).saturationNoise[0].layer.fractal.octaves = 0;
              }),
              "albedo.source.saturation_noise[0].octaves");
    EXPECT_EQ(error([&](TerrainConfig& c) { synthetic(c).valueNoise[0].hurst = NAN; }),
              "albedo.source.value_noise[0].hurst");
    EXPECT_EQ(error([&](TerrainConfig& c) { craters(c).craterLayer = 1; }),
              "albedo.layers[0].crater_layer");
    EXPECT_EQ(error([&](TerrainConfig& c) { craters(c).options.share = 2.0; }),
              "albedo.layers[0].share");
    EXPECT_EQ(error([](TerrainConfig& c) {
                  terraingen::SplotchAlbedoSpec splotches;
                  splotches.options.softness = 0.0;
                  c.albedo->layers.push_back(splotches);
              }),
              "albedo.layers[1].softness");
    EXPECT_EQ(error([](TerrainConfig& c) {
                  terraingen::BlastZoneAlbedoSpec blast;
                  terraingen::BlastZone empty;
                  empty.area = 0.0;
                  blast.zones = {terraingen::BlastZone{}, empty};
                  c.albedo->layers.push_back(blast);
              }),
              "albedo.layers[1].zones[1].area");
    EXPECT_EQ(
        error([](TerrainConfig& c) { c.albedo->layers.push_back(terraingen::RockAlbedoSpec{}); }),
        "albedo.layers[1]")
        << "rocks need a boulder layer";
    EXPECT_EQ(error([](TerrainConfig& c) {
                  c.layers.push_back(terraingen::BoulderLayerSpec{});
                  terraingen::RockAlbedoSpec rocks;
                  rocks.boulderLayer = 1;
                  c.albedo->layers.push_back(rocks);
              }),
              "albedo.layers[1].boulder_layer");
    EXPECT_EQ(error([](TerrainConfig& c) {
                  c.layers.push_back(terraingen::BoulderLayerSpec{});
                  terraingen::RockAlbedoSpec rocks;
                  rocks.options.reflectanceRatio = -1.0;
                  c.albedo->layers.push_back(rocks);
              }),
              "albedo.layers[1].reflectance_ratio");
    EXPECT_EQ(error([](TerrainConfig& c) {
                  terraingen::SlopeAlbedoSpec slopes;
                  slopes.options.baseline = 0.0;
                  c.albedo->layers.push_back(slopes);
              }),
              "albedo.layers[1].baseline");
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
