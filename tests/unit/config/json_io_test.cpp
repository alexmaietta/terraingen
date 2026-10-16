#include "configs.h"
#include "terraingen/layers/bedforms.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using terraingen::AgesSpec;
using terraingen::ConfigError;
using terraingen::ConfigToJson;
using terraingen::CraterLayerSpec;
using terraingen::CraterListOutput;
using terraingen::DensitySpec;
using terraingen::ListFormat;
using terraingen::LoadConfig;
using terraingen::ParseConfig;
using terraingen::TerrainConfig;
using terraingen::testing::ParseErrorPath;

namespace {

/// A configuration using a non-default alternative or value for everything.
TerrainConfig Everything() {
    TerrainConfig config;
    auto& source = std::get<terraingen::FlatSourceSpec>(config.source);
    source.width = 33;
    source.height = 21;
    source.pixelSize = 0.5;
    source.elevation = -1200.5f;
    terraingen::NoiseLayerSpec noise;
    terraingen::NoiseLayerConfig second;
    second.layer.blend = terraingen::LayerBlend::Mask;
    second.layer.fractal.basis = terraingen::NoiseBasis::WorleyF2MinusF1;
    second.layer.fractal.type = terraingen::FractalType::Ridged;
    second.layer.fractal.seed = 4000000000u;
    second.layer.warpAmplitude = 12.5f;
    second.hurst = 0.8f;
    noise.stack.push_back(second);
    CraterLayerSpec craters;
    craters.generate = false;
    craters.morphometry = terraingen::CraterMorphometryFit::Mars;
    craters.seed = 99;
    craters.density = terraingen::PiecewisePowerLawSpec{{{1.0, 0.2, 2.0}, {50.0, 10.0, 3.0}}};
    craters.ages = terraingen::SteadyStateAgesSpec{2.0, terraingen::ProductionFunction::Ivanov,
                                                   terraingen::EquilibriumModel::Hartmann,
                                                   terraingen::ChronologyModel::Ivanov};
    craters.minDiameter = 2.0;
    craters.maxDiameter = 90.0;
    craters.apply.rimSmoothing = {.sigmaRadii = 0.1, .strength = 0.5};
    craters.spacing = {2.0, 0.75};
    craters.maxCraters = 1234;
    craters.manual = {{1.5, -2.25, 30.0, 0.1, 7u}};
    craters.profile = terraingen::ComplexProfileSpec{terraingen::MartinParkesDunstanSpec{}};
    craters.degradation = terraingen::NoDegradationSpec{0.17};
    craters.apply = {
        .order = terraingen::CraterOrder::LargestFirst,
        .fade = {1.0, 3.0, terraingen::BlendShape::Linear},
        .datum = terraingen::CraterDatum::Plane,
        .datumFitRadii = 1.5,
        .interiorReplacement = 0.5,
        .blend = {0.7, 1.3, terraingen::BlendShape::Gaussian},
        .irregularity = {
            .enabled = true,
            .seed = 4,
            .selection =
                {.minDiameter = 10.0, .maxDiameter = 500.0, .minAgeGyr = 0.1, .manual = true},
            .roughShare = 0.5,
            .model = terraingen::RimRoughnessModel::Du2024,
            .scatter = false,
            .maxDeviation = 0.2,
            .roughness = {terraingen::ValueDistribution::Kind::Normal, 0.02, 0.005, 0.0, 0.05},
            .spectralSlope = {terraingen::ValueDistribution::Kind::Uniform, 1.0, 2.0},
            .minHarmonic = 3,
            .maxHarmonic = 8,
            .elongatedShare = 0.1,
            .ellipticity = {terraingen::ValueDistribution::Kind::LogNormal, 1.2, 0.1, 1.0, 2.23},
            .heading = 0.3,
            .headingSpread = 0.2}};
    terraingen::BoulderLayerSpec boulders;
    boulders.boulders = {
        .sizes = {.coverage = 0.05,
                  .qConstant = 1.5,
                  .qCoverageCoefficient = 0.2,
                  .minDiameter = 0.25,
                  .maxDiameter = 6.0},
        .seed = 17,
        .patches = {2.0, 80.0},
        .ejecta = {.minCraterDiameter = 10.0,
                   .regolithThickness = 3.0,
                   .excavationDepthRatio = 0.12,
                   .coverage = 0.2,
                   .profile = terraingen::EjectaProfile::Exponential,
                   .reach = 2.5,
                   .decayRadii = 0.6,
                   .floorShare = 0.1,
                   .aging = terraingen::EjectaAging::Age,
                   .minFreshness = 0.5,
                   .lifetimeGyr = 3.0,
                   .agingExponent = 2.0,
                   .gravity = 1.62},
        .bedrock = {.exposure = terraingen::ExposureParams{8.0, 0.02, 0.05, 0.1, 0.5, 0.3, 40.0, 5},
                    .abundance = 2.5,
                    .exposureSpacing = 0.5},
        .pose = {.minHeightRatio = 0.3,
                 .maxHeightRatio = 0.7,
                 .minBurial = 0.0,
                 .maxBurial = 0.5,
                 .maxLean = 0.1},
        .rendering = {.relief = terraingen::BoulderRelief::Shape,
                      .models = {.minDiameter = 2.0, .minSamples = 6.0},
                      .shapes = {.styles = {terraingen::BoulderStyle{
                                     .name = "slab",
                                     .weight = 2.0,
                                     .solid = terraingen::BaseSolid::Prism,
                                     .intermediate = {0.5, 0.7},
                                     .shortAxis = {0.1, 0.2},
                                     .rounding = {0.1, 0.3},
                                     .minFractures = 2,
                                     .maxFractures = 4,
                                     .fractureDepth = {0.5, 0.8},
                                     .maxFractureTilt = 0.2,
                                     .noiseAmplitude = 0.01,
                                     .noiseWavelength = 0.4,
                                     .noiseOctaves = 2}},
                                 .subdivisions = 1,
                                 .seed = 3},
                      .shapePrototypes = 0},
        .fade = {1.0, 3.0, terraingen::BlendShape::Linear}};
    boulders.sizes =
        terraingen::Sp8023IntercraterBouldersSpec{terraingen::SpecificationCurve::Maximum};
    boulders.ejectaSizes =
        terraingen::Sp8023CraterBouldersSpec{terraingen::SpecificationCurve::Minimum};
    boulders.extrapolate = true;
    terraingen::BoulderLayerSpec synthterrain;
    synthterrain.sizes =
        terraingen::SynthterrainBouldersSpec{terraingen::SynthterrainBoulders::ViperSpecification};
    terraingen::BoulderLayerSpec table;
    table.sizes = terraingen::TabulatedSpec{{0.1, 1.0}, {1.0, 0.001}};
    table.ejectaSizes = terraingen::PowerLawSpec{0.001, 3.0};
    const terraingen::RoughnessLayerSpec roughness{{.rmsSlope = 0.3,
                                                    .baseline = 0.05,
                                                    .hurst = 0.6,
                                                    .maxWavelength = 2.0,
                                                    .minWavelength = 0.005,
                                                    .seed = 9,
                                                    .fade = {1.5, 5.0}}};
    config.layers = {
        noise,
        craters,
        CraterLayerSpec{},
        boulders,
        terraingen::BoulderLayerSpec{},
        synthterrain,
        table,
        terraingen::BedformLayerSpec{terraingen::MarsLargeRipples()},
        roughness,
        terraingen::RoughnessLayerSpec{},
        terraingen::ScaleLayerSpec{1.5, -20.0},
        terraingen::ClampLayerSpec{std::nullopt, 12.5},
        terraingen::RegionLayerSpec{terraingen::CircleRegionSpec{3.0, -4.0, 50.0}, 7.5,
                                    terraingen::BlendShape::Gaussian, craters},
        terraingen::RegionLayerSpec{
            terraingen::RectangleRegionSpec{-1.0, -2.0, 3.0, 4.0}, 0.0,
            terraingen::BlendShape::Linear,
            terraingen::RegionLayerSpec{{}, 1.0, {}, terraingen::ClampLayerSpec{0.0, 1.0}}}};
    config.outputs = {
        CraterListOutput{"out/craters.json", ListFormat::Json},
        CraterListOutput{"craters.txt", ListFormat::Csv},
        terraingen::HeightmapOutput{"dem.tif", terraingen::TiffCompression::Deflate, 512},
        terraingen::BoulderListOutput{"boulders.json", ListFormat::Json},
        terraingen::BoulderModelsOutput{"boulders.glb", 20.0}};
    return config;
}

}  // namespace

TEST(ConfigJson, RoundTripsTheDefaultsAndEveryNonDefaultValue) {
    for (const TerrainConfig& config : {TerrainConfig{}, Everything()}) {
        const std::string json = ConfigToJson(config);
        EXPECT_EQ(ParseConfig(json), config) << json;
        EXPECT_EQ(ConfigToJson(ParseConfig(json)), json);
    }
}

TEST(ConfigJson, WritesTheRootKeysInOrderAndNoAlbedoWhenUnset) {
    const std::string json = ConfigToJson(TerrainConfig{});
    EXPECT_EQ(json.rfind("{\n  \"schema_version\": 1,\n  \"surface\": {", 0), 0u) << json;
    EXPECT_EQ(json.find("\"albedo\""), std::string::npos) << json;
    EXPECT_LT(json.find("\"layers\""), json.find("\"outputs\""));
    TerrainConfig config;
    config.albedo = terraingen::AlbedoSpec{};
    EXPECT_NE(ConfigToJson(config).find("\"albedo\""), std::string::npos);
}

TEST(ConfigJson, RoundTripsEveryModelType) {
    using terraingen::ChronologyModel;
    using terraingen::ProductionFunction;
    const std::vector<DensitySpec> densities{
        terraingen::ProductionSurfaceSpec{ProductionFunction::Neukum, ChronologyModel::Ivanov, 2.0},
        terraingen::ProductionSurfaceSpec{ProductionFunction::ExtendedNeukum,
                                          ChronologyModel::Neukum, 1.0},
        terraingen::ProductionSurfaceSpec{ProductionFunction::Ivanov, ChronologyModel::Ivanov, 1.5},
        terraingen::MinimumSpec{},
        terraingen::MinimumSpec{{terraingen::ProductionSurfaceSpec{}, terraingen::TraskSpec{},
                                 terraingen::MinimumSpec{{terraingen::PowerLawSpec{0.2, 2.1}}}}},
        terraingen::PowerLawSpec{0.3, 1.8},
        terraingen::TabulatedSpec{{1.0, 10.0}, {1.0, 0.01}, 1000.0, 1e6},
        terraingen::TraskSpec{},
        terraingen::HartmannSpec{},
        terraingen::MooreSpec{terraingen::MooreTerrain::Uplands},
        terraingen::ViperSpec{}};
    const std::vector<AgesSpec> ages{
        terraingen::FreshAgesSpec{}, terraingen::UniformAgesSpec{2.5},
        terraingen::SteadyStateAgesSpec{},
        terraingen::SteadyStateAgesSpec{3.0, ProductionFunction::Neukum,
                                        terraingen::EquilibriumModel::Hartmann,
                                        ChronologyModel::Neukum},
        terraingen::SteadyStateAgesSpec{1.0, ProductionFunction::Ivanov,
                                        terraingen::EquilibriumModel::Trask,
                                        ChronologyModel::Ivanov}};
    for (const terraingen::SourceSpec& source :
         {terraingen::SourceSpec{terraingen::RasterSourceSpec{
              "dem.tif", {2, 0.25, terraingen::ResampleMethod::Bilinear, {}, {}, 30.0}}},
          terraingen::SourceSpec{terraingen::RasterSourceSpec{"x.tif"}},
          terraingen::SourceSpec{terraingen::ChainSourceSpec{
              {terraingen::RasterSourceSpec{
                   "a.tif", {1, 2.0, terraingen::ResampleMethod::Nearest, 0.001, -250.0}},
               terraingen::FlatSourceSpec{3, 4, 5.0, 6.0f}, terraingen::ChainSourceSpec{}}}}}) {
        TerrainConfig config;
        config.source = source;
        EXPECT_EQ(ParseConfig(ConfigToJson(config)), config);
    }
    for (const DensitySpec& density : densities) {
        TerrainConfig config;
        CraterLayerSpec craters;
        craters.density = density;
        config.layers = {craters};
        EXPECT_EQ(ParseConfig(ConfigToJson(config)), config);
    }
    for (const AgesSpec& spec : ages) {
        TerrainConfig config;
        CraterLayerSpec craters;
        craters.ages = spec;
        craters.profile = terraingen::FassettThomsonSpec{terraingen::FassettThomsonRim::Plateau};
        config.layers = {craters};
        EXPECT_EQ(ParseConfig(ConfigToJson(config)), config);
    }
}

TEST(ConfigJson, RoundTripsAlbedos) {
    TerrainConfig config;
    terraingen::SyntheticAlbedoSpec synthetic;
    synthetic.rgb = true;
    synthetic.hue = 30.0;
    synthetic.saturation = 0.1;
    synthetic.value = 0.2;
    synthetic.valueNoise = {terraingen::NoiseLayerConfig{{}, 0.8f}};
    synthetic.hueNoise = {terraingen::NoiseLayerConfig{}};
    terraingen::CraterAlbedoSpec craters;
    craters.craterLayer = 1;
    craters.options.selection.maxDiameter = 5000.0;
    craters.options.selection.maxAgeGyr = 0.5;
    craters.options.rays.count = {terraingen::ValueDistribution::Kind::Uniform, 4.0, 9.0};
    craters.options.retention.largeMyr = 2000.0;
    craters.options.fadeExponent = 2.0;
    craters.options.rays.oblique.heading = 1.2;
    craters.options.rays.oblique.zoneHalfWidthDegrees = 30.0;
    craters.options.rays.oblique.downrangeSpeedRatio = {
        terraingen::ValueDistribution::Kind::Constant, 1.3};
    config.albedo = terraingen::AlbedoSpec{synthetic, {craters, terraingen::CraterAlbedoSpec{}}};
    config.outputs = {
        terraingen::AlbedoOutput{"albedo.tif", terraingen::TiffCompression::Deflate, 32}};
    const std::string json = ConfigToJson(config);
    EXPECT_EQ(ParseConfig(json), config) << json;
    EXPECT_EQ(ConfigToJson(ParseConfig(json)), json);

    terraingen::RasterAlbedoSpec raster{"wac.tif", {2, 1, 0}, 0.0001, 0.01, {}};
    raster.options.method = terraingen::ResampleMethod::Bilinear;
    raster.options.detail.hurst = 0.4;
    raster.options.detail.octaves = 6;
    config.albedo = terraingen::AlbedoSpec{raster, {}};
    EXPECT_EQ(ParseConfig(ConfigToJson(config)), config);

    const TerrainConfig parsed = ParseConfig(
        R"({"schema_version": 1, "albedo": {"source": {"type": "synthetic"},
            "layers": [{"type": "craters", "min_diameter": 100, "rays": {"enabled": false}}]}})");
    ASSERT_TRUE(parsed.albedo);
    const auto& layer = std::get<terraingen::CraterAlbedoSpec>(parsed.albedo->layers.at(0));
    EXPECT_EQ(layer.options.selection.minDiameter, 100.0);
    EXPECT_FALSE(layer.options.rays.enabled);
    EXPECT_EQ(layer.options.backgroundMaturity, 0.202);
    EXPECT_FALSE(layer.craterLayer);
    EXPECT_FALSE(ParseConfig(R"({"schema_version": 1})").albedo);
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "albedo": {"source": {"type": "map"}}})"),
              "albedo.source.type");
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "albedo": {"layers": [{"type": "craters",
              "rays": {"glow": 1}}]}})"),
              "albedo.layers[0].rays.glow");
}

TEST(ConfigJson, RoundTripsSurfacesAndFlatCentres) {
    TerrainConfig config;
    config.surface = terraingen::SphereSurfaceSpec{1737400.0};
    terraingen::FlatSourceSpec flat;
    flat.centreX = 30.0;
    flat.centreY = -45.0;
    config.source = flat;
    const std::string json = ConfigToJson(config);
    EXPECT_EQ(ParseConfig(json), config) << json;
    const TerrainConfig parsed =
        ParseConfig(R"({"schema_version": 1, "surface": {"type": "sphere", "radius": 3389500}})");
    EXPECT_EQ(std::get<terraingen::SphereSurfaceSpec>(parsed.surface).radius, 3389500.0);
    EXPECT_TRUE(std::holds_alternative<terraingen::PlaneSurfaceSpec>(
        ParseConfig(R"({"schema_version": 1})").surface));
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "surface": {"type": "torus"}})"),
              "surface.type");
    EXPECT_EQ(ParseConfig(ConfigToJson(parsed)), parsed);
    TerrainConfig meshed = parsed;
    meshed.outputs = {terraingen::BodyMeshOutput{"m.glb", 64, 300.0}};
    EXPECT_EQ(ParseConfig(ConfigToJson(meshed)), meshed);
    EXPECT_EQ(
        ParseErrorPath(R"({"schema_version": 1, "surface": {"type": "sphere", "body": "mars"}})"),
        "surface.body");
}

TEST(ConfigJson, ReadsProductionSurfacesMinimumsAndSteadyStateAges) {
    const TerrainConfig config = ParseConfig(R"({"schema_version": 1, "layers": [{
        "type": "craters", "morphometry": "mars",
        "density": {"type": "minimum", "distributions": [
            {"type": "production_surface", "production": "ivanov", "chronology": "ivanov",
             "age_gyr": 2.5},
            {"type": "trask"}]},
        "ages": {"type": "steady_state", "age_gyr": 2.5, "production": "neukum",
                 "equilibrium": "hartmann", "chronology": "ivanov"}}]})");
    const auto& craters = std::get<CraterLayerSpec>(config.layers.at(0));
    EXPECT_EQ(craters.morphometry, terraingen::CraterMorphometryFit::Mars);
    const terraingen::MinimumSpec expected{
        {terraingen::ProductionSurfaceSpec{terraingen::ProductionFunction::Ivanov,
                                           terraingen::ChronologyModel::Ivanov, 2.5},
         terraingen::TraskSpec{}}};
    EXPECT_EQ(craters.density, DensitySpec{expected});
    const terraingen::SteadyStateAgesSpec ages{2.5, terraingen::ProductionFunction::Neukum,
                                               terraingen::EquilibriumModel::Hartmann,
                                               terraingen::ChronologyModel::Ivanov};
    EXPECT_EQ(craters.ages, AgesSpec{ages});
    const TerrainConfig secondaries = ParseConfig(R"({"schema_version": 1, "layers": [
        {"type": "craters"}, {"type": "secondaries", "onset_count": 3, "morphometry": "lunar"}]})");
    EXPECT_EQ(std::get<terraingen::SecondaryLayerSpec>(secondaries.layers.at(1)).morphometry,
              terraingen::CraterMorphometryFit::Lunar);
}

TEST(ConfigJson, DefaultsCraterLayersToAPowerLawWithUniformAges) {
    const TerrainConfig config =
        ParseConfig(R"({"schema_version": 1, "layers": [{"type": "craters"}]})");
    const auto& craters = std::get<CraterLayerSpec>(config.layers.at(0));
    EXPECT_EQ(craters.density, DensitySpec{terraingen::PowerLawSpec{}});
    EXPECT_EQ(craters.ages, AgesSpec{terraingen::UniformAgesSpec{1.0}});
    EXPECT_EQ(craters.morphometry, terraingen::CraterMorphometryFit::Lunar);
    const auto& production = std::get<terraingen::ProductionSurfaceSpec>(
        std::get<CraterLayerSpec>(ParseConfig(R"({"schema_version": 1, "layers": [{"type":
            "craters", "density": {"type": "production_surface"}}]})")
                                      .layers.at(0))
            .density);
    EXPECT_EQ(production, terraingen::ProductionSurfaceSpec{});
}

TEST(ConfigJson, RejectsUnknownProductionsChronologiesAndMorphometries) {
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "layers": [{"type": "craters",
              "density": {"type": "production_surface", "production": "hartmann"}}]})"),
              "layers[0].density.production");
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "layers": [{"type": "craters",
              "density": {"type": "production_surface", "chronology": "mars"}}]})"),
              "layers[0].density.chronology");
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "layers": [{"type": "craters",
              "density": {"type": "minimum", "distributions": [{"type": "lunar_surface"}]}}]})"),
              "layers[0].density.distributions[0].type");
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "layers": [{"type": "craters",
              "ages": {"type": "steady_state", "chronology": "hartmann"}}]})"),
              "layers[0].ages.chronology");
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "layers": [{"type": "craters",
              "ages": {"type": "auto"}}]})"),
              "layers[0].ages.type");
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "layers": [{"type": "craters",
              "morphometry": "moon"}]})"),
              "layers[0].morphometry");
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "layers": [{"type": "craters",
              "body": "mars"}]})"),
              "layers[0].body");
}

TEST(ConfigJson, RoundTripsDiffusionDegradation) {
    for (const auto model : {terraingen::DiffusivityModel::Fassett2022Hartmann,
                             terraingen::DiffusivityModel::Fassett2022Trask,
                             terraingen::DiffusivityModel::FassettThomson2014,
                             terraingen::DiffusivityModel::FassettThomson2014Calibration,
                             terraingen::DiffusivityModel::PowerLaw}) {
        TerrainConfig config;
        CraterLayerSpec craters;
        terraingen::DiffusionDegradationSpec diffusion;
        diffusion.diffusivity = model;
        diffusion.powerLaw = {2.0, 0.9, 3.0};
        diffusion.reachRadii = 2.5;
        craters.degradation = diffusion;
        config.layers = {craters};
        const std::string json = ConfigToJson(config);
        EXPECT_EQ(ParseConfig(json), config) << json;
    }
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "layers": [{"type": "craters",
              "degradation": {"type": "diffusion", "diffusivity": "fast"}}]})"),
              "layers[0].degradation.diffusivity");
}

TEST(ConfigJson, RoundTripsSecondaryLayers) {
    TerrainConfig config;
    terraingen::SecondaryLayerSpec secondaries;
    secondaries.secondaries.onsetCount = 12.0;
    secondaries.secondaries.maxDiameter = 80.0;
    secondaries.secondaries.outerRange = 6.0;
    secondaries.secondaries.selection.maxAgeGyr = 0.5;
    secondaries.secondaries.selection.maxDiameter = 900.0;
    secondaries.depthRatio = 0.12;
    secondaries.morphometry = terraingen::CraterMorphometryFit::Mars;
    config.layers = {CraterLayerSpec{}, secondaries};
    const std::string json = ConfigToJson(config);
    EXPECT_EQ(ParseConfig(json), config) << json;
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "layers": [{"type": "secondaries",
              "onset_count": "many"}]})"),
              "layers[0].onset_count");
}

TEST(LoadConfig, ReadsFilesAndReportsUnreadableOnes) {
    const std::filesystem::path path =
        std::filesystem::path(::testing::TempDir()) / "terraingen_load_config.json";
    {
        std::ofstream file(path);
        file << ConfigToJson(Everything());
    }
    EXPECT_EQ(LoadConfig(path), Everything());
    std::filesystem::remove(path);
    EXPECT_THROW(LoadConfig(path), ConfigError);
}
