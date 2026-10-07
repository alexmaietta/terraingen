#include "terraingen/pipeline/config_json.h"

#include "terraingen/bodies/mars/bedforms.h"

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
    craters.body = terraingen::CraterBody::Mars;
    craters.seed = 99;
    craters.density = terraingen::PiecewisePowerLawSpec{{{1.0, 0.2, 2.0}, {50.0, 10.0, 3.0}}};
    craters.ages =
        terraingen::SteadyStateAgesSpec{2.0, terraingen::EquilibriumModel::Hartmann, false};
    craters.minDiameter = 2.0;
    craters.maxDiameter = 90.0;
    craters.spacing = {2.0, 0.75};
    craters.maxCraters = 1234;
    craters.manual = {{1.5, -2.25, 30.0, 0.1, 7u, true}};
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
    terraingen::RockLayerSpec rocks;
    rocks.rocks = {
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
        .relief = terraingen::RockRelief::Shape,
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
    rocks.sizes = terraingen::Sp8023IntercraterRocksSpec{terraingen::RockModelCurve::Maximum};
    rocks.ejectaSizes = terraingen::Sp8023CraterRocksSpec{terraingen::RockModelCurve::Minimum};
    rocks.extrapolate = true;
    terraingen::RockLayerSpec synthterrain;
    synthterrain.sizes =
        terraingen::SynthterrainRocksSpec{terraingen::SynthterrainRocks::ViperSpecification};
    terraingen::RockLayerSpec table;
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
        rocks,
        terraingen::RockLayerSpec{},
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
        CraterListOutput{"out/craters.json", CraterListFormat::Json},
        CraterListOutput{"craters.txt", CraterListFormat::Csv},
        terraingen::HeightmapOutput{"dem.tif", terraingen::TiffCompression::Deflate, 512},
        terraingen::RockListOutput{"rocks.json", CraterListFormat::Json},
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

TEST(ConfigJson, ReadsRockLayersWithOptionalExposure) {
    const TerrainConfig config = ParseConfig(R"({"schema_version": 1, "layers": [
        {"type": "rocks", "coverage": 0.04, "relief": "none", "ejecta": {"min_crater_diameter": 20},
         "exposure": {"reach": 6}},
        {"type": "rocks", "exposure": null}]})");
    ASSERT_EQ(config.layers.size(), 2u);
    const auto& rocks = std::get<terraingen::RockLayerSpec>(config.layers[0]).rocks;
    EXPECT_EQ(rocks.coverage, 0.04);
    EXPECT_EQ(rocks.relief, terraingen::RockRelief::None);
    EXPECT_EQ(rocks.ejecta.minCraterDiameter, 20.0);
    EXPECT_EQ(rocks.ejecta.coverage, terraingen::RockEjecta{}.coverage);
    ASSERT_TRUE(rocks.exposure.has_value());
    EXPECT_EQ(rocks.exposure->reach, 6.0);
    EXPECT_EQ(rocks.exposure->riseRock, terraingen::ExposureParams{}.riseRock);
    EXPECT_FALSE(std::get<terraingen::RockLayerSpec>(config.layers[1]).rocks.exposure);
    EXPECT_EQ(
        ErrorPath(R"({"schema_version": 1, "layers": [{"type": "rocks", "relief": "mesh"}]})"),
        "layers[0].relief");
    EXPECT_EQ(
        ErrorPath(R"({"schema_version": 1, "layers": [{"type": "rocks", "cache_rocks": 1}]})"),
        "layers[0].cache_rocks");
    EXPECT_EQ(
        ErrorPath(R"({"schema_version": 1, "layers": [{"type": "rocks", "exposure": {"x": 1}}]})"),
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
    EXPECT_EQ(ErrorPath(R"({"schema_version": 1, "layers": [{"type": "boulders"}]})"),
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
