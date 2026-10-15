/// @file
/// @brief Tests of reading and writing configurations as JSON (pipeline/json_io.cpp).
#include "terraingen/bodies/mars/bedforms.h"
#include "terraingen/pipeline/config_json.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

using terraingen::AgesSpec;
using terraingen::ConfigError;
using terraingen::ConfigToJson;
using terraingen::CraterLayerSpec;
using terraingen::CraterListOutput;
using terraingen::DensitySpec;
using terraingen::LayerSpec;
using terraingen::ListFormat;
using terraingen::LoadConfig;
using terraingen::ParseConfig;
using terraingen::TerrainConfig;

namespace {

/// The JSON path of the `ConfigError` that parsing `json` throws, or "no error".
std::string ErrorPath(const std::string& json) {
    try {
        ParseConfig(json);
    } catch (const ConfigError& error) {
        return error.Path();
    }
    return "no error";
}

/// The message of the `ConfigError` that parsing `json` throws, or "no error".
std::string ErrorMessage(const std::string& json) {
    try {
        ParseConfig(json);
    } catch (const ConfigError& error) {
        return error.what();
    }
    return "no error";
}

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
    craters.body = terraingen::CraterBody::Mars;
    craters.seed = 99;
    craters.density = terraingen::PiecewisePowerLawSpec{{{1.0, 0.2, 2.0}, {50.0, 10.0, 3.0}}};
    craters.ages =
        terraingen::SteadyStateAgesSpec{2.0, terraingen::EquilibriumModel::Hartmann, false};
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
            .minDiameter = 10.0,
            .maxDiameter = 500.0,
            .minAgeGyr = 0.1,
            .manual = true,
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
        .coverage = 0.05,
        .qa = 1.5,
        .qb = 0.2,
        .minDiameter = 0.25,
        .maxDiameter = 6.0,
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
        .exposure = terraingen::ExposureParams{8.0, 0.02, 0.05, 0.1, 0.5, 0.3, 40.0, 5},
        .bedrockAbundance = 2.5,
        .exposureSpacing = 0.5,
        .minHeightRatio = 0.3,
        .maxHeightRatio = 0.7,
        .minBurial = 0.0,
        .maxBurial = 0.5,
        .maxLean = 0.1,
        .relief = terraingen::BoulderRelief::Shape,
        .models = {.minDiameter = 2.0, .minSamples = 6.0},
        .shapes = {.styles = {terraingen::BoulderStyle{.name = "slab",
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
        .shapePrototypes = 0,
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
    config.filters = {terraingen::RimSmoothing{
        .sigmaRadii = 0.1, .strength = 0.5, .fade = {0.5, 6.0, terraingen::BlendShape::Gaussian}}};
    config.outputs = {
        CraterListOutput{"out/craters.json", ListFormat::Json},
        CraterListOutput{"craters.txt", ListFormat::Csv},
        terraingen::HeightmapOutput{"dem.tif", terraingen::TiffCompression::Deflate, 512},
        terraingen::BoulderListOutput{"boulders.json", ListFormat::Json},
        terraingen::BoulderModelsOutput{"boulders.glb", 20.0}};
    return config;
}

/// A configuration JSON and the path of the error parsing it reports.
struct ParseCase {
    const char* name;
    const char* json;
    const char* path;
};

const ParseCase kParseCases[] = {
    {"Truncated", "{", ""},
    {"NotAnObject", "[]", ""},
    {"NoSchemaVersion", "{}", "schema_version"},
    {"NewerSchemaVersion", R"({"schema_version": 2})", "schema_version"},
    {"NewerVersionWithNewKeys", R"({"schema_version": 2, "terrain": {}})", "schema_version"},
    {"SchemaVersionAsString", R"({"schema_version": "1"})", "schema_version"},
    {"HugeSchemaVersion", R"({"schema_version": 3000000000})", "schema_version"},
    {"NegativeSchemaVersion", R"({"schema_version": -3000000000})", "schema_version"},
    {"UnknownRootKey", R"({"schema_version": 1, "colour": 3})", "colour"},
    {"UnknownRootObject", R"({"schema_version": 1, "grid": {}})", "grid"},
    {"RetiredCratersKey", R"({"schema_version": 1, "craters": {}})", "craters"},
    {"NegativeWidth", R"({"schema_version": 1, "source": {"type": "flat", "width": -1}})",
     "source.width"},
    {"FractionalWidth", R"({"schema_version": 1, "source": {"type": "flat", "width": 2.5}})",
     "source.width"},
    {"HeightAsString", R"({"schema_version": 1, "source": {"type": "flat", "height": "tall"}})",
     "source.height"},
    {"PixelSizeAsString", R"({"schema_version": 1, "source": {"type": "flat", "pixel_size": "1"}})",
     "source.pixel_size"},
    {"PixelSizeOverflow",
     R"({"schema_version": 1, "source": {"type": "flat", "pixel_size": 1e400}})", ""},
    {"SourceAsArray", R"({"schema_version": 1, "source": []})", "source"},
    {"UnknownSourceType", R"({"schema_version": 1, "source": {"type": "dem"}})", "source.type"},
    {"SourceWithoutType", R"({"schema_version": 1, "source": {"width": 3}})", "source.type"},
    {"UnknownResampling",
     R"({"schema_version": 1, "source": {"type": "raster", "resampling": "lanczos"}})",
     "source.resampling"},
    {"UnknownRasterKey", R"({"schema_version": 1, "source": {"type": "raster", "layers": []}})",
     "source.layers"},
    {"StackAsObject", R"({"schema_version": 1, "layers": [{"type": "noise", "stack": {}}]})",
     "layers[0].stack"},
    {"RetiredLayerType", R"({"schema_version": 1, "layers": [{"type": "rocks"}]})",
     "layers[0].type"},
    {"UnknownFilterKey",
     R"({"schema_version": 1, "filters": [{"type": "rim_smoothing", "sigma": 1}]})",
     "filters[0].sigma"},
    {"UnknownCompression",
     R"({"schema_version": 1, "outputs": [{"type": "heightmap", "compression": "zstd"}]})",
     "outputs[0].compression"},
    {"UnknownNoiseKey",
     R"({"schema_version": 1, "layers": [{"type": "noise", "stack": [{}, {"octave": 3}]}]})",
     "layers[0].stack[1].octave"},
    {"UnknownBasis",
     R"({"schema_version": 1, "layers": [{"type": "noise", "stack": [{"basis": "curl"}]}]})",
     "layers[0].stack[0].basis"},
    {"FloatOverflow",
     R"({"schema_version": 1, "layers": [{"type": "noise", "stack": [{"amplitude": 1e300}]}]})",
     "layers[0].stack[0].amplitude"},
    {"IntegerOverflow",
     R"({"schema_version": 1, "layers": [{"type": "noise", "stack": [{"octaves": 1e12}]}]})",
     "layers[0].stack[0].octaves"},
    {"SeedOverflow",
     R"({"schema_version": 1, "layers": [{"type": "noise", "stack": [{"seed": 5000000000}]}]})",
     "layers[0].stack[0].seed"},
    {"EnabledAsNumber",
     R"({"schema_version": 1, "layers": [{"type": "noise", "stack": [{"enabled": 1}]}]})",
     "layers[0].stack[0].enabled"},
    {"DensityWithoutType",
     R"({"schema_version": 1, "layers": [{"type": "craters", "density": {"age_gyr": 3}}]})",
     "layers[0].density.type"},
    {"MisspelledDensity",
     R"({"schema_version": 1, "layers": [{"type": "craters", "density": {"type": "hartman"}}]})",
     "layers[0].density.type"},
    {"DensityAsString",
     R"({"schema_version": 1, "layers": [{"type": "craters", "density": "trask"}]})",
     "layers[0].density"},
    {"KeyOfAnotherDensity",
     R"({"schema_version": 1, "layers": [{"type": "craters", "density": {"type": "trask", "age_gyr": 3}}]})",
     "layers[0].density.age_gyr"},
    {"UnknownManualKey",
     R"({"schema_version": 1, "layers": [{"type": "craters", "manual": [{"x": 1, "pinned": true}]}]})",
     "layers[0].manual[0].pinned"},
    {"AgesTypeAsNumber",
     R"({"schema_version": 1, "layers": [{"type": "craters", "ages": {"type": 3}}]})",
     "layers[0].ages.type"},
    {"UnknownOutputType", R"({"schema_version": 1, "outputs": [{"type": "mesh"}]})",
     "outputs[0].type"},
    {"FormatAsNumber",
     R"({"schema_version": 1, "outputs": [{"type": "crater_list", "format": 1}]})",
     "outputs[0].format"},
    {"OutputsAsObject", R"({"schema_version": 1, "outputs": {}})", "outputs"},
    {"NullHurst",
     R"({"schema_version": 1, "layers": [{"type": "noise", "stack": [{"hurst": null}]}]})",
     "no error"},
    {"NullDepthRatio",
     R"({"schema_version": 1, "layers": [{"type": "craters", "degradation": {"type": "none", "depth_ratio": null}}]})",
     "no error"},
};

class ParseConfigPaths : public ::testing::TestWithParam<ParseCase> {};

}  // namespace

TEST_P(ParseConfigPaths, ReportsTheProblemByItsJsonPath) {
    EXPECT_EQ(ErrorPath(GetParam().json), GetParam().path) << GetParam().json;
}

INSTANTIATE_TEST_SUITE_P(Json, ParseConfigPaths, ::testing::ValuesIn(kParseCases),
                         [](const ::testing::TestParamInfo<ParseCase>& param) {
                             return std::string(param.param.name);
                         });

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
    EXPECT_LT(json.find("\"filters\""), json.find("\"outputs\""));
    TerrainConfig config;
    config.albedo = terraingen::AlbedoSpec{};
    EXPECT_NE(ConfigToJson(config).find("\"albedo\""), std::string::npos);
}

TEST(ConfigJson, RoundTripsEveryModelType) {
    const std::vector<DensitySpec> densities{
        terraingen::LunarSurfaceSpec{2.0},
        terraingen::MarsSurfaceSpec{1.5},
        terraingen::PowerLawSpec{0.3, 1.8},
        terraingen::TabulatedSpec{{1.0, 10.0}, {1.0, 0.01}, 1000.0, 1e6},
        terraingen::TraskSpec{},
        terraingen::HartmannSpec{},
        terraingen::MooreSpec{terraingen::MooreTerrain::Uplands},
        terraingen::ViperSpec{}};
    const std::vector<AgesSpec> ages{terraingen::AutoAgesSpec{}, terraingen::FreshAgesSpec{},
                                     terraingen::UniformAgesSpec{2.5}};
    for (const terraingen::SourceSpec& source :
         {terraingen::SourceSpec{terraingen::RasterSourceSpec{
              "dem.tif", 2, 0.25, terraingen::ResampleMethod::Bilinear}},
          terraingen::SourceSpec{terraingen::RasterSourceSpec{"x.tif", 0, std::nullopt,
                                                              terraingen::ResampleMethod::Cubic}},
          terraingen::SourceSpec{terraingen::ChainSourceSpec{
              {terraingen::RasterSourceSpec{"a.tif", 1, 2.0, terraingen::ResampleMethod::Nearest,
                                            0.001, -250.0},
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
    craters.options.maxDiameter = 5000.0;
    craters.options.maxAgeGyr = 0.5;
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
    EXPECT_EQ(layer.options.minDiameter, 100.0);
    EXPECT_FALSE(layer.options.rays.enabled);
    EXPECT_EQ(layer.options.backgroundMaturity, 0.202);
    EXPECT_FALSE(layer.craterLayer);
    EXPECT_FALSE(ParseConfig(R"({"schema_version": 1})").albedo);
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "albedo": {"source": {"type": "map"}}})"),
              "albedo.source.type");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "albedo": {"layers": [{"type": "craters",
              "rays": {"glow": 1}}]}})"),
              "albedo.layers[0].rays.glow");
}

TEST(ConfigJson, ReadsInfiniteBoundsAsUnset) {
    TerrainConfig config;
    terraingen::SecondaryLayerSpec secondaries;
    secondaries.secondaries.maxDiameter = 80.0;
    config.layers = {CraterLayerSpec{}, secondaries};
    const std::string json = ConfigToJson(config);
    EXPECT_NE(json.find("\"max_diameter\": 80.0"), std::string::npos) << json;
    EXPECT_NE(json.find("\"max_age_gyr\": null"), std::string::npos) << json;
    EXPECT_NE(json.find("\"min\": null"), std::string::npos) << "an unbounded distribution";
    const TerrainConfig parsed = ParseConfig(
        R"({"schema_version": 1, "layers": [{"type": "craters", "apply": {"irregularity":
            {"max_diameter": 30, "roughness": {"min": -1, "max": null}}}}]})");
    const auto& irregularity = std::get<CraterLayerSpec>(parsed.layers[0]).apply.irregularity;
    EXPECT_EQ(irregularity.maxDiameter, 30.0);
    EXPECT_EQ(irregularity.roughness.min, -1.0);
    EXPECT_EQ(irregularity.roughness.max, std::numeric_limits<double>::infinity());
    EXPECT_EQ(irregularity.maxAgeGyr, std::numeric_limits<double>::infinity());
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
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "surface": {"type": "torus"}})"), "surface.type");
    const TerrainConfig mars =
        ParseConfig(R"({"schema_version": 1, "surface": {"type": "sphere", "body": "mars"}})");
    EXPECT_EQ(std::get<terraingen::SphereSurfaceSpec>(mars.surface).body, terraingen::Body::Mars);
    EXPECT_EQ(ParseConfig(ConfigToJson(mars)), mars);
    TerrainConfig meshed = mars;
    meshed.outputs = {terraingen::BodyMeshOutput{"m.glb", 64, 300.0}};
    EXPECT_EQ(ParseConfig(ConfigToJson(meshed)), meshed);
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "surface": {"type": "sphere", "body": "pluto"}})"),
              "surface.body");
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
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "layers": [{"type": "craters",
              "degradation": {"type": "diffusion", "diffusivity": "fast"}}]})"),
              "layers[0].degradation.diffusivity");
}

TEST(ConfigJson, RoundTripsSecondaryLayers) {
    TerrainConfig config;
    terraingen::SecondaryLayerSpec secondaries;
    secondaries.secondaries.onsetCount = 12.0;
    secondaries.secondaries.maxDiameter = 80.0;
    secondaries.secondaries.outerRange = 6.0;
    secondaries.secondaries.maxAgeGyr = 0.5;
    secondaries.secondaries.maxPrimaryDiameter = 900.0;
    secondaries.depthRatio = 0.12;
    secondaries.body = terraingen::CraterBody::Mars;
    config.layers = {CraterLayerSpec{}, secondaries};
    const std::string json = ConfigToJson(config);
    EXPECT_EQ(ParseConfig(json), config) << json;
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "layers": [{"type": "secondaries",
              "onset_count": "many"}]})"),
              "layers[0].onset_count");
}

TEST(ConfigJson, OmittedKeysTakeDefaultsAndOmittedListsAreEmpty) {
    const TerrainConfig minimal = ParseConfig(R"({"schema_version": 1})");
    EXPECT_EQ(minimal.source, terraingen::SourceSpec{});
    EXPECT_TRUE(minimal.layers.empty());
    EXPECT_TRUE(minimal.filters.empty());
    EXPECT_TRUE(minimal.outputs.empty());
    const TerrainConfig partial = ParseConfig(R"({
        "schema_version": 1,
        "source": {"type": "flat", "width": 64},
        "layers": [{"type": "noise", "stack": [{"octaves": 3, "hurst": 0.7}]},
                   {"type": "craters", "density": {"type": "trask"}}],
        "filters": [{"type": "rim_smoothing"}]
    })");
    const auto& flat = std::get<terraingen::FlatSourceSpec>(partial.source);
    EXPECT_EQ(flat.width, 64u);
    EXPECT_EQ(flat.height, 513u);
    ASSERT_EQ(partial.layers.size(), 2u);
    const auto& noise = std::get<terraingen::NoiseLayerSpec>(partial.layers[0]);
    EXPECT_EQ(noise.stack.at(0).layer.fractal.octaves, 3);
    EXPECT_EQ(noise.stack.at(0).hurst, 0.7f);
    const auto& craters = std::get<CraterLayerSpec>(partial.layers[1]);
    EXPECT_TRUE(std::holds_alternative<terraingen::TraskSpec>(craters.density));
    EXPECT_EQ(craters.minDiameter, 4.0);
    ASSERT_EQ(partial.filters.size(), 1u);
    EXPECT_EQ(std::get<terraingen::RimSmoothing>(partial.filters[0]), terraingen::RimSmoothing{});
}

TEST(ConfigJson, ReadsBoulderLayersWithOptionalExposure) {
    const TerrainConfig config = ParseConfig(R"({"schema_version": 1, "layers": [
        {"type": "boulders", "coverage": 0.04, "relief": "none", "ejecta": {"min_crater_diameter": 20},
         "exposure": {"reach": 6}},
        {"type": "boulders", "exposure": null}]})");
    ASSERT_EQ(config.layers.size(), 2u);
    const auto& boulders = std::get<terraingen::BoulderLayerSpec>(config.layers[0]).boulders;
    EXPECT_EQ(boulders.coverage, 0.04);
    EXPECT_EQ(boulders.relief, terraingen::BoulderRelief::None);
    EXPECT_EQ(boulders.ejecta.minCraterDiameter, 20.0);
    EXPECT_EQ(boulders.ejecta.coverage, terraingen::BoulderEjecta{}.coverage);
    ASSERT_TRUE(boulders.exposure.has_value());
    EXPECT_EQ(boulders.exposure->reach, 6.0);
    EXPECT_EQ(boulders.exposure->riseBoulder, terraingen::ExposureParams{}.riseBoulder);
    EXPECT_FALSE(std::get<terraingen::BoulderLayerSpec>(config.layers[1]).boulders.exposure);
    EXPECT_EQ(
        ErrorPath(R"({"schema_version": 1, "layers": [{"type": "boulders", "relief": "mesh"}]})"),
        "layers[0].relief");
    EXPECT_EQ(
        ErrorPath(
            R"({"schema_version": 1, "layers": [{"type": "boulders", "cache_boulders": 1}]})"),
        "layers[0].cache_boulders");
    EXPECT_EQ(
        ErrorPath(
            R"({"schema_version": 1, "layers": [{"type": "boulders", "exposure": {"x": 1}}]})"),
        "layers[0].exposure.x");
}

TEST(Boxed, CopiesAndComparesByValue) {
    terraingen::Boxed<LayerSpec> a = terraingen::ScaleLayerSpec{2.0, 0.0};
    terraingen::Boxed<LayerSpec> b;
    EXPECT_TRUE(std::holds_alternative<terraingen::NoiseLayerSpec>(*b));
    b = a;
    EXPECT_EQ(a, b);
    std::get<terraingen::ScaleLayerSpec>(*b).scale = 3.0;
    EXPECT_EQ(std::get<terraingen::ScaleLayerSpec>(*a).scale, 2.0) << "deep copies";
    EXPECT_FALSE(a == b);
    const terraingen::Boxed<LayerSpec> c = std::move(b);
    EXPECT_EQ(std::get<terraingen::ScaleLayerSpec>(*c).scale, 3.0);
    EXPECT_EQ(c->index(), a->index());
}

TEST(ConfigJson, ReadsTransformsAndNestedRegions) {
    const TerrainConfig config = ParseConfig(R"({"schema_version": 1, "layers": [
        {"type": "scale", "scale": 2},
        {"type": "clamp", "min": -1},
        {"type": "region", "shape": {"type": "circle", "radius": 30}, "feather": 5,
         "layer": {"type": "region", "shape": {"type": "rectangle", "max_x": 1, "max_y": 2},
                   "layer": {"type": "clamp", "max": 0}}}]})");
    ASSERT_EQ(config.layers.size(), 3u);
    EXPECT_EQ(std::get<terraingen::ScaleLayerSpec>(config.layers[0]),
              (terraingen::ScaleLayerSpec{2.0, 0.0}));
    EXPECT_EQ(std::get<terraingen::ClampLayerSpec>(config.layers[1]),
              (terraingen::ClampLayerSpec{-1.0, std::nullopt}));
    const auto& outer = std::get<terraingen::RegionLayerSpec>(config.layers[2]);
    EXPECT_EQ(outer.shape,
              (terraingen::RegionShapeSpec{terraingen::CircleRegionSpec{0.0, 0.0, 30.0}}));
    EXPECT_EQ(outer.feather, 5.0);
    EXPECT_EQ(outer.blend, terraingen::BlendShape::Smoothstep);
    const auto& inner = std::get<terraingen::RegionLayerSpec>(*outer.layer);
    EXPECT_EQ(std::get<terraingen::RectangleRegionSpec>(inner.shape).maxY, 2.0);
    EXPECT_EQ(std::get<terraingen::ClampLayerSpec>(*inner.layer).max, 0.0);
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "layers": [
        {"type": "region", "layer": {"type": "clamp", "mid": 0}}]})"),
              "layers[0].layer.mid");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "layers": [
        {"type": "region", "shape": {"type": "polygon"}}]})"),
              "layers[0].shape.type");
}

TEST(ConfigJson, InfersTheListFormatFromTheExtension) {
    const TerrainConfig config = ParseConfig(R"({"schema_version": 1, "outputs": [
        {"type": "crater_list", "path": "a.json"},
        {"type": "crater_list", "path": "b.csv"},
        {"type": "crater_list", "path": "c.json", "format": "csv"},
        {"type": "boulder_list", "path": "d.json"},
        {"type": "boulder_list", "path": "e.txt"}]})");
    ASSERT_EQ(config.outputs.size(), 5u);
    EXPECT_EQ(std::get<CraterListOutput>(config.outputs[0]).format, ListFormat::Json);
    EXPECT_EQ(std::get<CraterListOutput>(config.outputs[1]).format, ListFormat::Csv);
    EXPECT_EQ(std::get<CraterListOutput>(config.outputs[2]).format, ListFormat::Csv);
    EXPECT_EQ(std::get<terraingen::BoulderListOutput>(config.outputs[3]).format, ListFormat::Json);
    EXPECT_EQ(std::get<terraingen::BoulderListOutput>(config.outputs[4]).format, ListFormat::Csv);
}

TEST(ConfigJson, ErrorMessagesNameThePathAndTheAlternatives) {
    const std::string message = ErrorMessage(
        R"({"schema_version": 1, "layers": [{"type": "craters", "profile": {"type": "bowl"}}]})");
    EXPECT_EQ(message.rfind("layers[0].profile.type: unknown type \"bowl\"", 0), 0u) << message;
    EXPECT_NE(message.find("fassett_thomson, martin_parkes_dunstan"), std::string::npos);
    EXPECT_EQ(ErrorMessage(R"({"schema_version": 2})"),
              "schema_version: version 2 is not supported; this library reads version 1");
    EXPECT_EQ(ErrorMessage("{}"), "schema_version: required; this library reads version 1");
    EXPECT_EQ(ErrorMessage(R"({"schema_version": 1, "colour": 3})")
                  .rfind("colour: unknown key; expected one of: ", 0),
              0u);
    EXPECT_EQ(ErrorMessage("[]"), "expected an object, not array");
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
