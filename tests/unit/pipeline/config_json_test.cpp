#include "terraingen/pipeline/config_json.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

using terraingen::AgesSpec;
using terraingen::ConfigError;
using terraingen::ConfigToJson;
using terraingen::CraterLayerSpec;
using terraingen::CraterListFormat;
using terraingen::CraterListOutput;
using terraingen::DensitySpec;
using terraingen::LayerSpec;
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
                     .fade = {1.0, 3.0, terraingen::BlendShape::Linear},
                     .datum = terraingen::CraterDatum::Plane,
                     .datumFitRadii = 1.5,
                     .interiorReplacement = 0.5,
                     .blend = {0.7, 1.3, terraingen::BlendShape::Gaussian}};
    config.layers = {noise, craters, CraterLayerSpec{}};
    config.filters = {terraingen::RimSmoothing{
        .sigmaRadii = 0.1, .strength = 0.5, .fade = {0.5, 6.0, terraingen::BlendShape::Gaussian}}};
    config.outputs = {
        CraterListOutput{"out/craters.json", CraterListFormat::Json},
        CraterListOutput{"craters.txt", CraterListFormat::Csv},
        terraingen::HeightmapOutput{"dem.tif", terraingen::TiffCompression::Deflate, 512}};
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
    for (const terraingen::SourceSpec& source :
         {terraingen::SourceSpec{terraingen::RasterSourceSpec{
              "dem.tif", 2, 0.25, terraingen::ResampleMethod::Bilinear}},
          terraingen::SourceSpec{terraingen::RasterSourceSpec{
              "x.tif", 0, std::nullopt, terraingen::ResampleMethod::Cubic}}}) {
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
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "source": {"type": "flat", "width": -1}})"),
              "source.width");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "source": {"type": "flat", "width": 2.5}})"),
              "source.width");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "source": {"type": "flat", "pixel_size": "1"}})"),
              "source.pixel_size");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "source": []})"), "source");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "grid": {}})"), "grid");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "source": {"type": "dem"}})"), "source.type");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "source": {"width": 3}})"), "source.type");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "layers": [{"type": "noise", "stack": {}}]})"),
              "layers[0].stack");
    EXPECT_EQ(
        ErrorPath(
            R"({"schema_version": 1, "source": {"type": "raster", "resampling": "lanczos"}})"),
        "source.resampling");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "source": {"type": "raster", "layers": []}})"),
              "source.layers");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "layers": [{"type": "rocks"}]})"),
              "layers[0].type");
    EXPECT_EQ(
        ErrorPath(R"({"schema_version": 1, "filters": [{"type": "rim_smoothing", "sigma": 1}]})"),
        "filters[0].sigma");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "craters": {}})"), "craters");
    EXPECT_EQ(
        ErrorPath(
            R"({"schema_version": 1, "outputs": [{"type": "heightmap", "compression": "zstd"}]})"),
        "outputs[0].compression");
    EXPECT_EQ(
        ErrorPath(
            R"({"schema_version": 1, "layers": [{"type": "noise", "stack": [{}, {"octave": 3}]}]})"),
        "layers[0].stack[1].octave");
    EXPECT_EQ(
        ErrorPath(
            R"({"schema_version": 1, "layers": [{"type": "noise", "stack": [{"basis": "curl"}]}]})"),
        "layers[0].stack[0].basis");
    EXPECT_EQ(
        ErrorPath(
            R"({"schema_version": 1, "layers": [{"type": "noise", "stack": [{"amplitude": 1e300}]}]})"),
        "layers[0].stack[0].amplitude");
    EXPECT_EQ(
        ErrorPath(
            R"({"schema_version": 1, "layers": [{"type": "noise", "stack": [{"octaves": 1e12}]}]})"),
        "layers[0].stack[0].octaves");
    EXPECT_EQ(
        ErrorPath(
            R"({"schema_version": 1, "layers": [{"type": "noise", "stack": [{"seed": 5000000000}]}]})"),
        "layers[0].stack[0].seed");
    EXPECT_EQ(
        ErrorPath(
            R"({"schema_version": 1, "layers": [{"type": "noise", "stack": [{"enabled": 1}]}]})"),
        "layers[0].stack[0].enabled");
    EXPECT_EQ(
        ErrorPath(
            R"({"schema_version": 1, "layers": [{"type": "craters", "density": {"age_gyr": 3}}]})"),
        "layers[0].density.type");
    EXPECT_EQ(
        ErrorPath(
            R"({"schema_version": 1, "layers": [{"type": "craters", "density": {"type": "hartman"}}]})"),
        "layers[0].density.type");
    EXPECT_EQ(
        ErrorPath(R"({"schema_version": 1, "layers": [{"type": "craters", "density": "trask"}]})"),
        "layers[0].density");
    EXPECT_EQ(
        ErrorPath(
            R"({"schema_version": 1, "layers": [{"type": "craters", "density": {"type": "trask", "age_gyr": 3}}]})"),
        "layers[0].density.age_gyr");
    EXPECT_EQ(
        ErrorPath(
            R"({"schema_version": 1, "layers": [{"type": "craters", "manual": [{"x": 1, "pinned": true}]}]})"),
        "layers[0].manual[0].pinned");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "outputs": [{"type": "mesh"}]})"),
              "outputs[0].type");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 3000000000})"), "schema_version");
    EXPECT_EQ(ErrorPath(R"({"schema_version": -3000000000})"), "schema_version");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "source": {"type": "flat", "height": "tall"}})"),
              "source.height");
    EXPECT_EQ(
        ErrorPath(R"({"schema_version": 1, "source": {"type": "flat", "pixel_size": 1e400}})"), "");
    EXPECT_EQ(
        ErrorPath(R"({"schema_version": 1, "outputs": [{"type": "crater_list", "format": 1}]})"),
        "outputs[0].format");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "outputs": {}})"), "outputs");
    EXPECT_EQ(
        ErrorPath(R"({"schema_version": 1, "layers": [{"type": "craters", "ages": {"type": 3}}]})"),
        "layers[0].ages.type");
    EXPECT_EQ(
        ErrorPath(
            R"({"schema_version": 1, "layers": [{"type": "noise", "stack": [{"hurst": null}]}]})"),
        "no error");
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1,
                            "layers": [{"type": "craters", "degradation": {"type": "none", "depth_ratio": null}}]})"),
              "no error");
}

TEST(ConfigJson, ErrorMessagesNameThePathAndTheAlternatives) {
    try {
        ParseConfig(
            R"({"schema_version": 1, "layers": [{"type": "craters", "profile": {"type": "bowl"}}]})");
        FAIL() << "expected a ConfigError";
    } catch (const ConfigError& error) {
        const std::string message = error.what();
        EXPECT_EQ(message.rfind("layers[0].profile.type: unknown type \"bowl\"", 0), 0u) << message;
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
