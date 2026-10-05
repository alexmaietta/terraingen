#include "terraingen/pipeline/config_json.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

using terraingen::AgesSpec;
using terraingen::ConfigError;
using terraingen::ConfigToJson;
using terraingen::CraterConfig;
using terraingen::CraterListFormat;
using terraingen::CraterListOutput;
using terraingen::DensitySpec;
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

/// A configuration using a non-default alternative or value for everything.
TerrainConfig Everything() {
    TerrainConfig config;
    config.grid = {33, 21, 0.5};
    terraingen::NoiseLayerConfig second;
    second.layer.blend = terraingen::LayerBlend::Mask;
    second.layer.fractal.basis = terraingen::NoiseBasis::WorleyF2MinusF1;
    second.layer.fractal.type = terraingen::FractalType::Ridged;
    second.layer.fractal.seed = 4000000000u;
    second.layer.warpAmplitude = 12.5f;
    second.hurst = 0.8f;
    config.surface.layers.push_back(second);
    CraterConfig& craters = *config.craters;
    craters.generate = false;
    craters.seed = 99;
    craters.density = terraingen::PiecewisePowerLawSpec{{{1.0, 0.2, 2.0}, {50.0, 10.0, 3.0}}};
    craters.ages =
        terraingen::SteadyStateAgesSpec{2.0, terraingen::EquilibriumModel::Hartmann, false};
    craters.minDiameter = 2.0;
    craters.maxDiameter = 90.0;
    craters.spacing = {2.0, 0.75};
    craters.maxCraters = 1234;
    craters.manual = {{1.5, -2.25, 30.0, 0.1, 7u, true}};
    craters.profile = terraingen::MartinParkesDunstanSpec{};
    craters.degradation = terraingen::NoDegradationSpec{0.17};
    craters.apply = {.order = terraingen::CraterOrder::LargestFirst,
                     .minDiameterPixels = 4.0,
                     .datum = terraingen::CraterDatum::Plane,
                     .datumFitRadii = 1.5,
                     .interiorReplacement = 0.5,
                     .blend = {0.7, 1.3, terraingen::BlendShape::Gaussian}};
    craters.rimSmoothing = {.sigmaRadii = 0.1, .strength = 0.5};
    config.outputs = {CraterListOutput{"out/craters.json", CraterListFormat::Json},
                      CraterListOutput{"craters.txt", CraterListFormat::Csv}};
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

TEST(ConfigJson, RoundTripsEveryModelType) {
    const std::vector<DensitySpec> densities{
        terraingen::LunarSurfaceSpec{2.0},
        terraingen::PowerLawSpec{0.3, 1.8},
        terraingen::TabulatedSpec{{1.0, 10.0}, {1.0, 0.01}, 1000.0, 1e6},
        terraingen::TraskSpec{},
        terraingen::HartmannSpec{},
        terraingen::MooreSpec{terraingen::MooreTerrain::Uplands},
        terraingen::ViperSpec{}};
    const std::vector<AgesSpec> ages{terraingen::AutoAgesSpec{}, terraingen::FreshAgesSpec{},
                                     terraingen::UniformAgesSpec{2.5}};
    for (const DensitySpec& density : densities) {
        TerrainConfig config;
        config.craters->density = density;
        EXPECT_EQ(ParseConfig(ConfigToJson(config)), config);
    }
    for (const AgesSpec& spec : ages) {
        TerrainConfig config;
        config.craters->ages = spec;
        config.craters->profile =
            terraingen::FassettThomsonSpec{terraingen::FassettThomsonRim::Plateau};
        EXPECT_EQ(ParseConfig(ConfigToJson(config)), config);
    }
}

TEST(ConfigJson, OmittedKeysTakeDefaultsAndOmittedCratersDisablesThem) {
    const TerrainConfig minimal = ParseConfig(R"({"schema_version": 1})");
    EXPECT_EQ(minimal.grid, terraingen::GridConfig{});
    EXPECT_EQ(minimal.surface, terraingen::SurfaceConfig{});
    EXPECT_FALSE(minimal.craters.has_value());
    EXPECT_FALSE(ParseConfig(R"({"schema_version": 1, "craters": null})").craters.has_value());
    const TerrainConfig partial = ParseConfig(R"({
        "schema_version": 1,
        "grid": {"width": 64},
        "surface": {"type": "synthetic", "layers": [{"octaves": 3, "hurst": 0.7}]},
        "craters": {"density": {"type": "trask"}}
    })");
    EXPECT_EQ(partial.grid.width, 64u);
    EXPECT_EQ(partial.grid.height, 513u);
    EXPECT_EQ(partial.surface.layers.at(0).layer.fractal.octaves, 3);
    EXPECT_EQ(partial.surface.layers.at(0).hurst, 0.7f);
    ASSERT_TRUE(partial.craters.has_value());
    EXPECT_TRUE(std::holds_alternative<terraingen::TraskSpec>(partial.craters->density));
    EXPECT_EQ(partial.craters->minDiameter, 4.0);
}

TEST(ConfigJson, InfersTheCraterListFormatFromTheExtension) {
    const TerrainConfig config = ParseConfig(R"({"schema_version": 1, "outputs": [
        {"type": "crater_list", "path": "a.json"},
        {"type": "crater_list", "path": "b.csv"},
        {"type": "crater_list", "path": "c.json", "format": "csv"}]})");
    ASSERT_EQ(config.outputs.size(), 3u);
    EXPECT_EQ(std::get<CraterListOutput>(config.outputs[0]).format, CraterListFormat::Json);
    EXPECT_EQ(std::get<CraterListOutput>(config.outputs[1]).format, CraterListFormat::Csv);
    EXPECT_EQ(std::get<CraterListOutput>(config.outputs[2]).format, CraterListFormat::Csv);
}

TEST(ConfigJson, ReportsProblemsByTheirJsonPath) {
    EXPECT_EQ(ErrorPath("{"), "");
    EXPECT_EQ(ErrorPath("[]"), "");
    EXPECT_EQ(ErrorPath("{}"), "schema_version");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 2})"), "schema_version");
    EXPECT_EQ(ErrorPath(R"({"schema_version": "1"})"), "schema_version");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "colour": 3})"), "colour");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "grid": {"width": -1}})"), "grid.width");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "grid": {"width": 2.5}})"), "grid.width");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "grid": {"pixel_size": "1"}})"),
              "grid.pixel_size");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "grid": []})"), "grid");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "surface": {"type": "dem"}})"), "surface.type");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "surface": {"layers": {}}})"), "surface.layers");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "surface": {"layers": [{}, {"octave": 3}]}})"),
              "surface.layers[1].octave");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "surface": {"layers": [{"basis": "curl"}]}})"),
              "surface.layers[0].basis");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "surface": {"layers": [{"amplitude": 1e300}]}})"),
              "surface.layers[0].amplitude");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "surface": {"layers": [{"octaves": 1e12}]}})"),
              "surface.layers[0].octaves");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "surface": {"layers": [{"seed": 5000000000}]}})"),
              "surface.layers[0].seed");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "surface": {"layers": [{"enabled": 1}]}})"),
              "surface.layers[0].enabled");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "craters": {"density": {"age_gyr": 3}}})"),
              "craters.density.type");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "craters": {"density": {"type": "hartman"}}})"),
              "craters.density.type");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "craters": {"density": "trask"}})"),
              "craters.density");
    EXPECT_EQ(
        ErrorPath(
            R"({"schema_version": 1, "craters": {"density": {"type": "trask", "age_gyr": 3}}})"),
        "craters.density.age_gyr");
    EXPECT_EQ(
        ErrorPath(R"({"schema_version": 1, "craters": {"manual": [{"x": 1, "pinned": true}]}})"),
        "craters.manual[0].pinned");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "outputs": [{"type": "heightmap"}]})"),
              "outputs[0].type");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 3000000000})"), "schema_version");
    EXPECT_EQ(ErrorPath(R"({"schema_version": -3000000000})"), "schema_version");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "grid": {"height": "tall"}})"), "grid.height");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "grid": {"pixel_size": 1e400}})"), "");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "outputs": [{"type": "crater_list", "format": 1}]})"),
              "outputs[0].format");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "outputs": {}})"), "outputs");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "craters": {"ages": {"type": 3}}})"),
              "craters.ages.type");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "surface": {"layers": [{"hurst": null}]}})"),
              "no error");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1,
                            "craters": {"degradation": {"type": "none", "depth_ratio": null}}})"),
              "no error");
}

TEST(ConfigJson, ErrorMessagesNameThePathAndTheAlternatives) {
    try {
        ParseConfig(R"({"schema_version": 1, "craters": {"profile": {"type": "bowl"}}})");
        FAIL() << "expected a ConfigError";
    } catch (const ConfigError& error) {
        const std::string message = error.what();
        EXPECT_EQ(message.rfind("craters.profile.type: unknown type \"bowl\"", 0), 0u) << message;
        EXPECT_NE(message.find("fassett_thomson, martin_parkes_dunstan"), std::string::npos);
    }
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
