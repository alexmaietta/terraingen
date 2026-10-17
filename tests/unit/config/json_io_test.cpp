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
    config.body = terraingen::SphereSurfaceSpec{3389500.0};
    config.base = terraingen::RasterSourceSpec{"dem.tif"};
    terraingen::PavementOptions plates;
    plates.plateSize = 0.8;
    plates.crackWidth = 0.03;
    plates.maxTilt = 0.05;
    plates.octaves = 3;
    plates.lacunarity = 2.5;
    plates.gain = 0.6;
    terraingen::DuneOptions flowing = terraingen::MarsTransverseDunes();
    flowing.grainflows = terraingen::MarsDuneGrainflows();
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
    craters.apply.rimSmoothing = {.sigmaRadii = 0.1, .strength = 0.5};
    craters.apply.noise = {.amplitude = 0.01, .octaves = 3, .rimAmplitude = 0.02, .seed = 5};
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
        .ventifacts = {.share = 0.39,
                       .windAzimuth = 0.8,
                       .minAngle = 0.9,
                       .maxAngle = 1.0,
                       .secondWindAzimuth = 4.0,
                       .keelShare = 0.3},
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
        terraingen::DuneLayerSpec{terraingen::MarsBarchans()},
        terraingen::DuneLayerSpec{terraingen::MarsTransverseDunes()},
        terraingen::DuneLayerSpec{flowing},
        terraingen::SandFillLayerSpec{{6.0, 0.4, 0.3, 0.05}},
        terraingen::PavementLayerSpec{terraingen::MarsDesiccationRidges()},
        terraingen::PavementLayerSpec{plates},
        terraingen::StrataLayerSpec{
            {3.5, 0.2, 0.05, 1.2, 6.0, 0.8, 0.25, 9u, {137.4, -4.6}, 0.6, 2.0}},
        terraingen::StrataLayerSpec{},
        terraingen::SandShadowLayerSpec{
            {1.2, 2.5, 0.5, 0.3, 0.01, {1.0, 3.0, terraingen::BlendShape::Linear}}},
        terraingen::SandShadowLayerSpec{},
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
    const terraingen::GridRegionSpec region{33, 21, 0.5, 30.0, -45.0};
    config.outputs = {
        CraterListOutput{"out/craters.json", ListFormat::Json, region, 4.0},
        CraterListOutput{"craters.txt", ListFormat::Csv, {}, 100.0},
        terraingen::HeightmapOutput{"dem.tif", terraingen::TiffCompression::Deflate, 512, region},
        terraingen::BoulderListOutput{"boulders.json", ListFormat::Json, region},
        terraingen::BoulderModelsOutput{"boulders.glb", 20.0, region, 2.0},
        terraingen::MeshOutput{"body.icq", 64, 300.0},
        terraingen::TerrainMeshOutput{"site.glb", region},
        terraingen::MaterialsOutput{
            {"materials.tif", terraingen::TiffCompression::Deflate, 64, region}},
        terraingen::PhotometryOutput{{"hapke.tif", terraingen::TiffCompression::Lzw, 256, region}}};
    terraingen::MaterialsSpec materials;
    materials.options.exposure->reach = 20.0;
    materials.options.dustCover = 0.25;
    materials.options.dustPatch = 30.0;
    materials.options.seed = 9;
    materials.photometry.reference = terraingen::PhotometryReference::MarsPancam;
    materials.photometry.classes[3] = terraingen::HapkeParameters{0.7, 0.4, 0.3, 15.0};
    config.materials = materials;
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
    EXPECT_EQ(json.rfind("{\n  \"schema_version\": 1,\n  \"body\": {", 0), 0u) << json;
    EXPECT_EQ(json.find("\"base\""), std::string::npos) << json;
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
               terraingen::ChainSourceSpec{}}}}}) {
        TerrainConfig config;
        config.base = source;
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
    terraingen::SplotchAlbedoSpec splotches;
    splotches.options.minDiameter = 3.0;
    splotches.options.darkChange = {terraingen::ValueDistribution::Kind::Uniform, -0.06, -0.02};
    splotches.options.swarms.enabled = true;
    splotches.options.swarms.craters = {{10.0, -20.0, 18.8, 0.0, 0u, false}};
    terraingen::BlastZoneAlbedoSpec blast;
    blast.zones = {terraingen::BlastZone{.x = 5.0, .area = 2530.0, .coreRadius = 3.0}};
    config.albedo = terraingen::AlbedoSpec{
        synthetic, {craters, terraingen::CraterAlbedoSpec{}, splotches, blast}};
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

TEST(ConfigJson, RoundTripsBodiesBasesAndRegions) {
    const TerrainConfig parsed =
        ParseConfig(R"({"schema_version": 1, "body": {"type": "sphere", "radius": 3389500}})");
    EXPECT_EQ(std::get<terraingen::SphereSurfaceSpec>(parsed.body).radius, 3389500.0);
    EXPECT_EQ(ParseConfig(ConfigToJson(parsed)), parsed);
    const TerrainConfig ellipsoid = ParseConfig(
        R"({"schema_version": 1, "body": {"type": "ellipsoid", "radii": [270, 147, 104]}})");
    EXPECT_EQ(std::get<terraingen::EllipsoidSurfaceSpec>(ellipsoid.body).radii,
              (std::vector<double>{270.0, 147.0, 104.0}));
    EXPECT_EQ(ParseConfig(ConfigToJson(ellipsoid)), ellipsoid);
    const TerrainConfig shape = ParseConfig(R"({"schema_version": 1, "body": {
        "type": "shape_model", "path": "itokawa.icq", "q": 64, "metres_per_unit": 1000}})");
    EXPECT_EQ(std::get<terraingen::ShapeModelSurfaceSpec>(shape.body),
              (terraingen::ShapeModelSurfaceSpec{"itokawa.icq", 64, 1000.0}));
    EXPECT_EQ(ParseConfig(ConfigToJson(shape)), shape);
    EXPECT_TRUE(std::holds_alternative<terraingen::PlaneSurfaceSpec>(
        ParseConfig(R"({"schema_version": 1})").body));
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "body": {"type": "torus"}})"), "body.type");
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "body": {"type": "sphere", "body": 1}})"),
              "body.body");

    const TerrainConfig based = ParseConfig(R"({"schema_version": 1,
        "base": {"type": "raster", "path": "dem.tif"},
        "outputs": [{"type": "mesh", "path": "m.glb", "cells_per_face": 64},
                    {"type": "heightmap", "path": "h.tif",
                     "region": {"width": 10, "height": 20, "pixel_size": 4,
                                "centre_x": 30, "centre_y": -45}}]})");
    EXPECT_EQ(std::get<terraingen::RasterSourceSpec>(*based.base).path, "dem.tif");
    EXPECT_EQ(std::get<terraingen::MeshOutput>(based.outputs.at(0)).cellsPerFace, 64u);
    EXPECT_EQ(std::get<terraingen::HeightmapOutput>(based.outputs.at(1)).region,
              (terraingen::GridRegionSpec{10, 20, 4.0, 30.0, -45.0}));
    EXPECT_EQ(ParseConfig(ConfigToJson(based)), based);
    for (const char* removed : {R"({"schema_version": 1, "surface": {"type": "plane"}})",
                                R"({"schema_version": 1, "source": {"type": "raster"}})",
                                R"({"schema_version": 1, "base": {"type": "flat"}})",
                                R"({"schema_version": 1, "outputs": [{"type": "body_mesh"}]})"}) {
        EXPECT_NE(ParseErrorPath(removed), "no error") << removed;
    }
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
    const TerrainConfig surface = ParseConfig(R"({"schema_version": 1, "layers": [{"type":
        "craters", "density": {"type": "production_surface"}}]})");
    const auto& production = std::get<terraingen::ProductionSurfaceSpec>(
        std::get<CraterLayerSpec>(surface.layers.at(0)).density);
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
    secondaries.secondaries.rays.enabled = true;
    secondaries.secondaries.rays.layout.seed = 4;
    secondaries.secondaries.rays.clustering = 0.5;
    secondaries.secondaries.shapes.elongatedShare = 0.3;
    secondaries.morphometry = terraingen::CraterMorphometryFit::Mars;
    config.layers = {CraterLayerSpec{}, secondaries};
    terraingen::CraterAlbedoSpec bright;
    bright.options.rays.layout = terraingen::RayLayout{.seed = 4};
    config.albedo = terraingen::AlbedoSpec{terraingen::SyntheticAlbedoSpec{}, {bright}};
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
