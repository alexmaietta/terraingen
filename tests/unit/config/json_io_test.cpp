#include "configs.h"
#include "terraingen/layers/bedforms.h"

#include <gtest/gtest.h>

#include <cmath>
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
    terraingen::RasterSourceSpec raster{"dem.tif"};
    raster.read.region = terraingen::Bounds2d{-10.0, -5.0, 20.5, 15.0};
    config.base = raster;
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
    noise.estimateFromBase = true;
    CraterLayerSpec craters;
    craters.generateRandom = false;
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
    craters.manual = {{1.5, -2.25, 30.0, 0.1, 7u}, {6.0, 4.0, 6.0, 0.0, 0u, false, 0.9, 0.25}};
    craters.profile = terraingen::ComplexProfileSpec{terraingen::MartinParkesDunstanSpec{}};
    craters.degradation = terraingen::NoDegradationSpec{0.17};
    craters.apply = terraingen::CraterApplyOptions{};
    craters.apply.order = terraingen::CraterOrder::LargestFirst;
    craters.apply.fade = {1.0, 3.0, terraingen::BlendShape::Linear};
    craters.apply.datum = terraingen::CraterDatum::Plane;
    craters.apply.datumFitRadii = 1.5;
    craters.apply.interiorReplacement = 0.5;
    craters.apply.blend = {0.7, 1.3, terraingen::BlendShape::Gaussian};
    terraingen::CraterIrregularity& irregularity = craters.apply.irregularity;
    irregularity.enabled = true;
    irregularity.seed = 4;
    irregularity.selection = terraingen::CraterSelection{};
    irregularity.selection.minDiameter = 10.0;
    irregularity.selection.maxDiameter = 500.0;
    irregularity.selection.minAgeGyr = 0.1;
    irregularity.selection.manual = true;
    irregularity.roughShare = 0.5;
    irregularity.model = terraingen::RimRoughnessModel::Du2024;
    irregularity.scatter = false;
    irregularity.maxDeviation = 0.2;
    irregularity.roughness = {terraingen::ValueDistribution::Kind::Normal, 0.02, 0.005, 0.0, 0.05};
    irregularity.spectralSlope = {terraingen::ValueDistribution::Kind::Uniform, 1.0, 2.0};
    irregularity.minHarmonic = 3;
    irregularity.maxHarmonic = 8;
    irregularity.elongatedShare = 0.1;
    irregularity.ellipticity = {terraingen::ValueDistribution::Kind::LogNormal, 1.2, 0.1, 1.0,
                                2.23};
    irregularity.heading = 0.3;
    irregularity.headingSpread = 10.0 * terraingen::kDegree;
    craters.apply.rimSmoothing.sigmaRadii = 0.1;
    craters.apply.rimSmoothing.strength = 0.5;
    craters.apply.noise.amplitude = 0.01;
    craters.apply.noise.octaves = 3;
    craters.apply.noise.rimAmplitude = 0.02;
    craters.apply.noise.seed = 5;
    craters.apply.irregularity.marksShare = 0.4;
    craters.apply.irregularity.marksAmplitude = {terraingen::ValueDistribution::Kind::Uniform, 0.0,
                                                 0.01};
    craters.apply.irregularity.marksKnots = 30;
    craters.apply.irregularity.marksInnerRadii = 0.35;
    craters.apply.irregularity.marksOuterRadii = 0.8;
    craters.apply.irregularity.marksFadeRadii = 0.12;
    craters.apply.slope.enabled = true;
    craters.apply.slope.seed = 6;
    craters.apply.slope.floorOffset = {terraingen::ValueDistribution::Kind::Constant, 0.1};
    craters.apply.slope.upslopeRim = 0.2;
    craters.apply.slope.fullSlope = 6.0 * terraingen::kDegree;
    craters.apply.slope.criticalGradient = 0.7;
    craters.apply.slope.maxWearRate = 5.0;
    terraingen::DensityMapSpec craterMap;
    craterMap.field.path = "map.tif";
    craterMap.field.band = 1;
    craterMap.field.method = terraingen::ResampleMethod::Nearest;
    craterMap.field.scale = 0.001;
    craterMap.field.fallback = 2.0;
    craterMap.field.periodicLongitude = true;
    craterMap.measure = terraingen::DensityMeasure::Count;
    craterMap.minDiameter = 4.5;
    craterMap.maxDiameter = 1000.0;
    craterMap.areaUnit = 1e6;
    craterMap.total = false;
    craters.densityMap = craterMap;
    terraingen::FieldSpec craterCompleteness;
    craterCompleteness.value = 1500.0;
    craters.completeness = craterCompleteness;
    craters.unresolvedPixelsAcross = 4.0;
    terraingen::BoulderLayerSpec boulders;
    boulders.boulders = terraingen::BoulderLayerOptions{};
    terraingen::BoulderLayerOptions& options = boulders.boulders;
    options.sizes.coverage = 0.05;
    options.sizes.qConstant = 1.5;
    options.sizes.qCoverageCoefficient = 0.2;
    options.sizes.minDiameter = 0.25;
    options.sizes.maxDiameter = 6.0;
    options.seed = 17;
    options.patches = {2.0, 80.0};
    options.ejecta.minCraterDiameter = 10.0;
    options.ejecta.regolithThickness = 3.0;
    options.ejecta.excavationDepthRatio = 0.12;
    options.ejecta.coverage = 0.2;
    options.ejecta.profile = terraingen::EjectaProfile::Exponential;
    options.ejecta.reach = 2.5;
    options.ejecta.decayRadii = 0.6;
    options.ejecta.floorShare = 0.1;
    options.ejecta.aging = terraingen::EjectaAging::Age;
    options.ejecta.minFreshness = 0.5;
    options.ejecta.lifetimeGyr = 3.0;
    options.ejecta.agingExponent = 2.0;
    options.ejecta.gravity = 1.62;
    options.bedrock.exposure = terraingen::ExposureParams{8.0, 0.02, 0.05, 0.1, 0.5, 0.3, 40.0, 5};
    options.bedrock.abundance = 2.5;
    options.pose.minHeightRatio = 0.3;
    options.pose.maxHeightRatio = 0.7;
    options.pose.minBurial = 0.0;
    options.pose.maxBurial = 0.5;
    options.pose.maxLean = 6.0 * terraingen::kDegree;
    options.pose.maxSlope = 0.6;
    options.pose.rollDistance = 25.0;
    options.ventifacts.share = 0.39;
    options.ventifacts.windAzimuth = 0.8;
    options.ventifacts.minAngle = 0.9;
    options.ventifacts.maxAngle = 1.0;
    options.ventifacts.secondWindAzimuth = 4.0;
    options.ventifacts.keelShare = 0.3;
    options.rendering.relief = terraingen::BoulderRelief::Shape;
    options.rendering.models.minDiameter = 2.0;
    options.rendering.models.minSamples = 6.0;
    terraingen::BoulderStyle slab;
    slab.name = "slab";
    slab.weight = 2.0;
    slab.solid = terraingen::BaseSolid::Prism;
    slab.intermediate = {0.5, 0.7};
    slab.shortAxis = {0.1, 0.2};
    slab.rounding = {0.1, 0.3};
    slab.minFractures = 2;
    slab.maxFractures = 4;
    slab.fractureDepth = {0.5, 0.8};
    slab.maxFractureTilt = 0.2;
    slab.noiseAmplitude = 0.01;
    slab.noiseWavelength = 0.4;
    slab.noiseOctaves = 2;
    options.rendering.shapes.styles = {slab};
    options.rendering.shapes.subdivisions = 1;
    options.rendering.shapes.seed = 3;
    options.rendering.shapePrototypes = 0;
    options.fade = {1.0, 3.0, terraingen::BlendShape::Linear};
    options.groundSpacing = 0.5;
    boulders.sizes =
        terraingen::Sp8023IntercraterBouldersSpec{terraingen::SpecificationCurve::Maximum};
    boulders.ejectaSizes =
        terraingen::Sp8023CraterBouldersSpec{terraingen::SpecificationCurve::Minimum};
    boulders.extrapolate = true;
    boulders.boulders.clusters = {0.4, 12.0, 6.0, terraingen::BoulderClusterModel::Thomas};
    terraingen::BoulderLayerSpec synthterrain;
    synthterrain.sizes =
        terraingen::SynthterrainBouldersSpec{terraingen::SynthterrainBoulders::ViperSpecification};
    terraingen::BoulderLayerSpec table;
    table.sizes = terraingen::TabulatedSpec{{0.1, 1.0}, {1.0, 0.001}};
    table.ejectaSizes = terraingen::PowerLawSpec{0.001, 3.0};
    table.boulders.generateRandom = false;
    synthterrain.ejectaSizes = terraingen::PiecewiseUniformSpec{{{0.5, 2.0, 0.001}}};
    terraingen::DensityMapSpec boulderMap;
    boulderMap.field.value = 0.01;
    boulderMap.measure = terraingen::DensityMeasure::ArealFraction;
    boulderMap.minDiameter = 1.0;
    synthterrain.densityMap = boulderMap;
    terraingen::ManualBoulder posed{1.0, 2.0, 1.5, 11u};
    posed.height = 0.8;
    posed.burial = 0.3;
    posed.yaw = 30.0 * terraingen::kDegree;
    posed.lean = 10.0 * terraingen::kDegree;
    posed.tilt = 20.0 * terraingen::kDegree;
    posed.tiltAzimuth = -72.5 * terraingen::kDegree;
    posed.wear = 0.4;
    posed.rest = terraingen::BoulderRest::End;
    posed.shapeSeed = 42u;
    synthterrain.manual = {{3.0, -4.0, 2.5}, posed};
    terraingen::FieldSpec boulderCompleteness;
    boulderCompleteness.path = "complete.tif";
    synthterrain.completeness = boulderCompleteness;
    terraingen::SizeSlopeMapSpec slopeMap;
    slopeMap.field.path = "mean.tif";
    slopeMap.measure = terraingen::SizeSlopeMeasure::MeanDiameter;
    slopeMap.referenceDiameter = 4.5;
    slopeMap.minExponent = 1.5;
    slopeMap.maxExponent = 5.0;
    synthterrain.slopeMap = slopeMap;
    terraingen::RoughnessLayerSpec roughness;
    roughness.rmsSlope = 0.3;
    roughness.roughness.baseline = 0.05;
    roughness.hurst = 0.6;
    roughness.roughness.maxWavelength = 2.0;
    roughness.roughness.minWavelength = 0.005;
    roughness.roughness.seed = 9;
    roughness.roughness.fade = {1.5, 5.0};
    roughness.estimateFromBase = true;
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
        terraingen::MidpointDisplacementLayerSpec{4.0, 1.5, -2.5, 2.3, 0.4, 0.25,
                                                  terraingen::MidpointBase::Source, 5u},
        terraingen::MidpointDisplacementLayerSpec{},
        terraingen::SpectralContinuationLayerSpec{
            2.0, 2.8, 0.15, {3.0, 40.0}, 0.1, 0.5, 9u, {1.5, 5.0, terraingen::BlendShape::Linear}},
        terraingen::SpectralContinuationLayerSpec{},
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
        terraingen::TerrainMeshOutput{
            "holed.obj", region, {{-1.0, -2.0, 3.0, 4.0}}, 2.5, 30.0, -45.0},
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

TEST(ConfigJson, RoundTripsLayersThatEstimateFromTheBase) {
    const TerrainConfig config = ParseConfig(R"({"schema_version": 1,
        "layers": [{"type": "roughness", "estimate_from_base": true, "hurst": 0.7},
                   {"type": "noise", "estimate_from_base": true, "stack": [{"wavelength": 4}]}]})");
    const auto& roughness = std::get<terraingen::RoughnessLayerSpec>(config.layers[0]);
    EXPECT_TRUE(roughness.estimateFromBase);
    EXPECT_EQ(roughness.hurst, 0.7);
    EXPECT_FALSE(roughness.rmsSlope);
    const auto& noise = std::get<terraingen::NoiseLayerSpec>(config.layers[1]);
    EXPECT_TRUE(noise.estimateFromBase);
    EXPECT_FALSE(noise.stack[0].amplitude);
    const std::string json = ConfigToJson(config);
    EXPECT_NE(json.find("\"rms_slope\": null"), std::string::npos) << json;
    EXPECT_EQ(ParseConfig(json), config);
    const TerrainConfig plain =
        ParseConfig(R"({"schema_version": 1, "layers": [{"type": "roughness"}]})");
    EXPECT_FALSE(std::get<terraingen::RoughnessLayerSpec>(plain.layers[0]).estimateFromBase);
}

TEST(ConfigJson, ReadsGeoTiffRegionsAndUnresolvedCraterLimits) {
    const TerrainConfig config = ParseConfig(R"({"schema_version": 1,
        "base": {"type": "raster", "path": "dem.tif",
                 "region": {"min_x": 1000, "min_y": 1900, "max_x": 1100.5, "max_y": 1950}},
        "layers": [{"type": "craters", "unresolved_pixels_across": 3.5},
                   {"type": "dem_detail", "path": "dem.tif",
                    "region": {"min_x": -1, "min_y": -2, "max_x": 3, "max_y": 4}}]})");
    EXPECT_EQ(std::get<terraingen::RasterSourceSpec>(*config.base).read.region,
              (terraingen::Bounds2d{1000.0, 1900.0, 1100.5, 1950.0}));
    EXPECT_EQ(std::get<CraterLayerSpec>(config.layers.at(0)).unresolvedPixelsAcross, 3.5);
    EXPECT_EQ(std::get<terraingen::DemDetailLayerSpec>(config.layers.at(1)).read.region,
              (terraingen::Bounds2d{-1.0, -2.0, 3.0, 4.0}));
    EXPECT_EQ(ParseConfig(ConfigToJson(config)), config);
    const TerrainConfig unset = ParseConfig(R"({"schema_version": 1,
        "base": {"type": "raster", "path": "dem.tif"}, "layers": [{"type": "craters"}]})");
    EXPECT_FALSE(std::get<terraingen::RasterSourceSpec>(*unset.base).read.region);
    EXPECT_FALSE(std::get<CraterLayerSpec>(unset.layers.at(0)).unresolvedPixelsAcross);
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "base": {"type": "raster",
        "path": "dem.tif", "region": {"min_x": 0, "west": 1}}})"),
              "base.region.west");
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "base": {"type": "raster",
        "path": "dem.tif", "region": [0, 0, 1, 1]}})"),
              "base.region");
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "layers": [{"type": "craters",
        "unresolved_pixels_across": "four"}]})"),
              "layers[0].unresolved_pixels_across");
}

TEST(ConfigJson, ReadsAndWritesManualCraterDepthsAndRimHeights) {
    const TerrainConfig config = ParseConfig(R"({"schema_version": 1, "layers": [{"type": "craters",
        "manual": [{"x": 1, "y": 2, "diameter": 6, "depth": 0.9},
                   {"x": 3, "y": 4, "diameter": 6, "depth": 0.9, "rim_height": 0.2},
                   {"x": 5, "y": 6, "diameter": 6}]}]})");
    const auto& manual = std::get<CraterLayerSpec>(config.layers[0]).manual;
    ASSERT_EQ(manual.size(), 3u);
    EXPECT_EQ(manual[0].depth, 0.9);
    EXPECT_TRUE(std::isnan(manual[0].rimHeight));
    EXPECT_EQ(manual[1].rimHeight, 0.2);
    EXPECT_TRUE(std::isnan(manual[2].depth));
    const std::string json = ConfigToJson(config);
    const std::size_t last = json.find("\"x\": 5.0,");
    ASSERT_NE(last, std::string::npos) << json;
    const std::string unset = json.substr(last, json.find('}', last) - last);
    EXPECT_NE(unset.find("\"depth\": null"), std::string::npos) << unset;
    EXPECT_NE(unset.find("\"rim_height\": null"), std::string::npos) << unset;
    EXPECT_NE(json.find("\"depth\": 0.9,\n          \"rim_height\": 0.2"), std::string::npos)
        << json;
    EXPECT_EQ(ParseConfig(json), config);
}

TEST(ConfigJson, WritesTheRootKeysInOrderAndNoAlbedoWhenUnset) {
    const std::string json = ConfigToJson(TerrainConfig{});
    EXPECT_EQ(json.rfind("{\n  \"schema_version\": 1,\n  \"body\": {", 0), 0u) << json;
    EXPECT_EQ(json.find("\"base\""), std::string::npos) << json;
    EXPECT_EQ(json.find("\"albedo\""), std::string::npos) << json;
    EXPECT_EQ(json.find("\"layers\""), std::string::npos) << "empty lists are left out";
    const std::string small = ConfigToJson(terraingen::testing::Small());
    EXPECT_LT(small.find("\"layers\""), small.find("\"outputs\"")) << small;
    TerrainConfig config;
    config.albedo = terraingen::AlbedoSpec{};
    EXPECT_NE(ConfigToJson(config).find("\"albedo\""), std::string::npos);
}

TEST(ConfigJson, LeavesOutListsThatAreEmptyByDefault) {
    TerrainConfig config;
    config.layers.push_back(CraterLayerSpec{});
    terraingen::NoiseLayerSpec flat;
    flat.stack.clear();
    config.layers.push_back(flat);
    const std::string json = ConfigToJson(config);
    EXPECT_EQ(json.find("\"edges\""), std::string::npos) << json;
    EXPECT_EQ(json.find("\"weights\""), std::string::npos) << json;
    EXPECT_NE(json.find("\"stack\": []"), std::string::npos)
        << "an empty list whose default is not empty stays: " << json;
    EXPECT_EQ(ParseConfig(json), config);
    EXPECT_NO_THROW(ParseConfig(R"({"schema_version": 1, "layers": [{"type": "craters",
        "ages": {"type": "freshness", "freshness": {"kind": "uniform", "a": 0, "b": 1,
        "edges": [], "weights": []}}}]})"));
    EXPECT_NE(terraingen::ConfigCatalog().find("\"edges\": []"), std::string::npos)
        << "the catalog lists every field";
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
        terraingen::PiecewiseUniformSpec{{{0.5, 1.0, 0.5}, {1.5, 3.0, 0.05}}, 0.2},
        terraingen::TraskSpec{},
        terraingen::HartmannSpec{},
        terraingen::MooreSpec{terraingen::MooreTerrain::Uplands},
        terraingen::ViperSpec{}};
    terraingen::ValueDistribution piecewise{terraingen::ValueDistribution::Kind::Piecewise};
    piecewise.edges = {0.0, 0.5, 1.0};
    piecewise.weights = {1.0, 2.0};
    const std::vector<AgesSpec> ages{
        terraingen::FreshAgesSpec{},
        terraingen::UniformAgesSpec{2.5},
        terraingen::FreshnessAgesSpec{},
        terraingen::FreshnessAgesSpec{
            0.8, {terraingen::ValueDistribution::Kind::Normal, 0.35, 0.1, 0.0, 1.0}},
        terraingen::FreshnessAgesSpec{2.0, piecewise},
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
    synthetic.valueNoise = {terraingen::NoiseLayerConfig{{}, 0.8f, {}}};
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
    terraingen::BlastZone zone;
    zone.x = 5.0;
    zone.area = 2530.0;
    zone.coreRadius = 3.0;
    blast.zones = {zone};
    terraingen::RockAlbedoSpec rocks;
    rocks.boulderLayer = 0;
    rocks.options.reflectanceRatio = 1.5;
    rocks.options.unresolved = false;
    terraingen::SlopeAlbedoSpec slopes;
    slopes.options.baseline = 2.0;
    slopes.options.onsetDegrees = 25.0;
    config.albedo = terraingen::AlbedoSpec{
        synthetic, {craters, terraingen::CraterAlbedoSpec{}, splotches, blast, rocks, slopes}};
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

TEST(ConfigJson, ReadsFreshnessAgesWithTheirDistribution) {
    const TerrainConfig config = ParseConfig(R"({"schema_version": 1, "layers": [{
        "type": "craters",
        "ages": {"type": "freshness", "lifetime_gyr": 1.5,
                 "freshness": {"kind": "normal", "a": 0.35, "b": 0.1}}}]})");
    const terraingen::FreshnessAgesSpec normal{
        1.5, {terraingen::ValueDistribution::Kind::Normal, 0.35, 0.1, 0.0, 1.0}};
    EXPECT_EQ(std::get<CraterLayerSpec>(config.layers.at(0)).ages, AgesSpec{normal});
    const TerrainConfig shaped = ParseConfig(R"({"schema_version": 1, "layers": [{
        "type": "craters",
        "ages": {"type": "freshness",
                 "freshness": {"kind": "piecewise", "edges": [0, 0.3, 1], "weights": [2, 1]}}}]})");
    const auto& ages = std::get<terraingen::FreshnessAgesSpec>(
        std::get<CraterLayerSpec>(shaped.layers.at(0)).ages);
    EXPECT_EQ(ages.lifetimeGyr, 1.0);
    EXPECT_EQ(ages.freshness.edges, (std::vector<double>{0.0, 0.3, 1.0}));
    EXPECT_EQ(ages.freshness.weights, (std::vector<double>{2.0, 1.0}));
    EXPECT_EQ(ParseConfig(ConfigToJson(shaped)), shaped);
}

TEST(ConfigJson, DefaultsCraterLayersToASteadyStateSurfaceBelowTrask) {
    const TerrainConfig config =
        ParseConfig(R"({"schema_version": 1, "layers": [{"type": "craters"}]})");
    const auto& craters = std::get<CraterLayerSpec>(config.layers.at(0));
    const terraingen::MinimumSpec density{
        {terraingen::ProductionSurfaceSpec{}, terraingen::HartmannSpec{}}};
    EXPECT_EQ(craters.density, DensitySpec{density});
    const terraingen::SteadyStateAgesSpec ages{3.5, terraingen::ProductionFunction::ExtendedNeukum,
                                               terraingen::EquilibriumModel::Hartmann,
                                               terraingen::ChronologyModel::Neukum};
    EXPECT_EQ(craters.ages, AgesSpec{ages});
    EXPECT_TRUE(std::holds_alternative<terraingen::DiffusionDegradationSpec>(craters.degradation));
    EXPECT_TRUE(craters.apply.irregularity.enabled);
    EXPECT_EQ(craters.minDiameter, 2.5);
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
    bright.options.rays.layout = terraingen::RayLayout{4};
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
