#include "terraingen/pipeline/run.h"

#include "terraingen/pipeline/config_json.h"
#include "terraingen/raster/geotiff.h"
#include "terraingen/raster/resample.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using terraingen::Bounds2d;
using terraingen::BuildCraterModels;
using terraingen::ConfigError;
using terraingen::CraterLayerSpec;
using terraingen::CraterList;
using terraingen::CraterListFormat;
using terraingen::CraterListOutput;
using terraingen::GenerateTerrain;
using terraingen::TerrainConfig;
using terraingen::ValidateConfig;

namespace {

terraingen::FlatSourceSpec& Flat(TerrainConfig& config) {
    return std::get<terraingen::FlatSourceSpec>(config.source);
}

/// The noise layer of a `Small` configuration.
terraingen::NoiseLayerSpec& Noise(TerrainConfig& config) {
    return std::get<terraingen::NoiseLayerSpec>(config.layers.at(0));
}

/// The crater layer of a `Small` configuration.
CraterLayerSpec& Craters(TerrainConfig& config) {
    return std::get<CraterLayerSpec>(config.layers.at(1));
}

/// A small, fast configuration: a flat source, a noise layer, then a crater layer.
TerrainConfig Small() {
    TerrainConfig config;
    config.source = terraingen::FlatSourceSpec{65, 49, 2.0};
    terraingen::NoiseLayerSpec noise;
    noise.stack[0].layer.fractal.wavelength = 200.0f;
    noise.stack[0].layer.fractal.amplitude = 10.0f;
    CraterLayerSpec craters;
    craters.minDiameter = 8.0;
    craters.maxDiameter = 60.0;
    config.layers = {noise, craters};
    return config;
}

/// The JSON path of the `ConfigError` validating `config` throws, or "valid".
template <typename Mutate> std::string ErrorPath(Mutate mutate) {
    TerrainConfig config = Small();
    mutate(config);
    try {
        ValidateConfig(config);
    } catch (const ConfigError& error) {
        return error.Path();
    }
    return "valid";
}

std::string Read(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

void ExpectSameRaster(const terraingen::Raster<float>& a, const terraingen::Raster<float>& b) {
    ASSERT_EQ(a.Width(), b.Width());
    ASSERT_EQ(a.Height(), b.Height());
    for (std::size_t i = 0; i < a.Size(); ++i) {
        ASSERT_EQ(a.Values()[i], b.Values()[i]) << i;
    }
}

std::filesystem::path TempPath(const std::string& name) {
    return std::filesystem::path(::testing::TempDir()) / name;
}

}  // namespace

TEST(ValidateConfig, AcceptsTheDefaultsAndReportsEachProblemByPath) {
    EXPECT_NO_THROW(ValidateConfig(TerrainConfig{}));
    EXPECT_EQ(ErrorPath([](TerrainConfig&) {}), "valid");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { c.schemaVersion = 7; }), "schema_version");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { Flat(c).width = 1; }), "source.width");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { Flat(c).height = 100000; }), "source.width");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { Flat(c).pixelSize = 0.0; }), "source.pixel_size");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { Noise(c).stack.clear(); }), "layers[0].stack");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { Noise(c).stack[0].layer.fractal.octaves = 0; }),
              "layers[0].stack[0]");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { Noise(c).stack[0].hurst = 1.0f / 0.0f; }),
              "layers[0].stack[0].hurst");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { c.outputs = {terraingen::HeightmapOutput{}}; }),
              "outputs[0].path");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) {
                  c.outputs = {
                      terraingen::HeightmapOutput{"a.tif", terraingen::TiffCompression::PackBits}};
              }),
              "outputs[0].compression");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) {
                  c.outputs = {
                      terraingen::HeightmapOutput{"a.tif", terraingen::TiffCompression::Lzw, 20}};
              }),
              "outputs[0].tile_size");
    EXPECT_EQ(
        ErrorPath([](TerrainConfig& c) { Craters(c).density = terraingen::LunarSurfaceSpec{9.0}; }),
        "layers[1].density");
    EXPECT_EQ(
        ErrorPath([](TerrainConfig& c) { Craters(c).ages = terraingen::UniformAgesSpec{0.0}; }),
        "layers[1].ages");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) {
                  Craters(c).degradation = terraingen::FreshnessDegradationSpec{0.5};
              }),
              "layers[1].degradation");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { Craters(c).spacing.diameterRatio = 0.5; }),
              "layers[1].spacing");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { Craters(c).apply.interiorReplacement = 2.0; }),
              "layers[1].apply");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) {
                  c.filters = {terraingen::RimSmoothing{},
                               terraingen::RimSmoothing{.strength = -1.0}};
              }),
              "filters[1]");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { Craters(c).minDiameter = 0.0; }),
              "layers[1].min_diameter");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { Craters(c).maxDiameter = 5.0; }),
              "layers[1].max_diameter");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { Craters(c).minDiameter = 2.0; }),
              "layers[1].min_diameter");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) {
                  Craters(c).density = terraingen::TraskSpec{};
                  Craters(c).minDiameter = 0.001;
              }),
              "layers[1].min_diameter");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { Craters(c).manual = {{0.0, 0.0, -1.0, 0.0}}; }),
              "layers[1].manual[0].diameter");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { Craters(c).manual = {{0.0, 0.0, 5.0, -1.0}}; }),
              "layers[1].manual[0].age_gyr");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { Craters(c).manual = {{1.0 / 0.0, 0.0, 5.0, 0.0}}; }),
              "layers[1].manual[0]");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) {
                  Craters(c).generate = false;
                  Craters(c).minDiameter = -1.0;
              }),
              "valid");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { c.outputs = {CraterListOutput{}}; }),
              "outputs[0].path");
}

TEST(ValidateConfig, ReportsRockLayerProblemsByPath) {
    const auto withRocks = [](auto change) {
        return ErrorPath([&](TerrainConfig& c) {
            terraingen::RockLayerSpec rocks;
            rocks.rocks.minDiameter = 1.0;
            change(rocks.rocks);
            c.layers.push_back(rocks);
        });
    };
    EXPECT_EQ(withRocks([](terraingen::RockLayerOptions&) {}), "valid");
    EXPECT_EQ(withRocks([](terraingen::RockLayerOptions& r) { r.coverage = 2.0; }), "layers[2]");
    EXPECT_EQ(withRocks([](terraingen::RockLayerOptions& r) { r.ejecta.minCraterDiameter = 10.0; }),
              "valid");
    EXPECT_EQ(withRocks([](terraingen::RockLayerOptions& r) { r.minDiameter = 0.01; }), "valid");
    EXPECT_EQ(withRocks([](terraingen::RockLayerOptions& r) { r.maxDiameter = 2.0; }), "valid");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) {
                  terraingen::RockLayerSpec rocks;
                  rocks.rocks.ejecta.minCraterDiameter = 10.0;
                  c.layers.insert(c.layers.begin() + 1, rocks);
              }),
              "layers[1].ejecta.min_crater_diameter");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) {
                  Flat(c) = {60000, 60000, 0.1};
                  terraingen::RockLayerSpec rocks;
                  rocks.rocks.coverage = 0.5;
                  c.layers = {rocks};
              }),
              "layers[0].min_diameter");
}

TEST(GenerateTerrain, AddsRoughnessAndReportsItsProblemsByPath) {
    TerrainConfig config = Small();
    const auto plain = GenerateTerrain(config);
    config.layers.push_back(
        terraingen::RoughnessLayerSpec{{.rmsSlope = 0.2, .minWavelength = 1.0}});
    const auto rough = GenerateTerrain(config);
    const terraingen::RoughnessLayer layer({.rmsSlope = 0.2, .minWavelength = 1.0});
    const auto& heights = plain.heights;
    for (std::size_t row = 0; row < heights.Height(); row += 7) {
        for (std::size_t col = 0; col < heights.Width(); col += 5) {
            const auto centre = heights.CellCenter(col, row);
            EXPECT_EQ(rough.heights(col, row),
                      layer.ApplyAt(heights(col, row), centre.x, centre.y, {2.0}));
        }
    }
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) {
                  c.layers.push_back(terraingen::RoughnessLayerSpec{{.hurst = 2.0}});
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
    terraingen::RockLayerSpec rocks;
    rocks.rocks.ejecta.minCraterDiameter = 10.0;
    EXPECT_EQ(ErrorPath([&](TerrainConfig& c) {
                  c.layers = {RegionLayerSpec{{}, 1.0, {}, Craters(c)}, rocks};
              }),
              "layers[1].ejecta.min_crater_diameter")
        << "crater layers inside regions do not count for ejecta";
    EXPECT_EQ(ErrorPath([&](TerrainConfig& c) {
                  c.layers.push_back(RegionLayerSpec{{}, 1.0, {}, rocks});
              }),
              "valid")
        << "rocks inside a region see the crater layers before it";
}

TEST(GenerateTerrain, AppliesTransformsAndRegionsAsTheLibraryDoes) {
    TerrainConfig config = Small();
    const terraingen::RegionLayerSpec region{terraingen::CircleRegionSpec{10.0, -5.0, 20.0}, 6.0,
                                             terraingen::BlendShape::Linear,
                                             terraingen::ClampLayerSpec{std::nullopt, 0.0}};
    config.layers.push_back(terraingen::ScaleLayerSpec{2.0, 1.0});
    config.layers.push_back(region);
    const auto generated = GenerateTerrain(config);

    terraingen::Terrain terrain = terraingen::BuildTerrain(config);
    ASSERT_EQ(terrain.layers.size(), 4u);
    terrain.layers[2] = std::make_shared<terraingen::ScaleLayer>(2.0, 1.0);
    terrain.layers[3] = std::make_shared<terraingen::RegionLayer>(
        std::make_shared<terraingen::ClampLayer>(std::nullopt, 0.0),
        terraingen::RegionShape::Circle({10.0, -5.0}, 20.0), 6.0, terraingen::BlendShape::Linear);
    ExpectSameRaster(generated.heights, terraingen::Evaluate(terrain).heights);
    const auto& heights = generated.heights;
    const auto centre = heights.Transform().WorldToPixel(10.0, -5.0);
    EXPECT_LE(heights(static_cast<std::size_t>(centre.x), static_cast<std::size_t>(centre.y)),
              0.0f);
}

TEST(GenerateTerrain, DrawsRocksFromEachKindOfSizeDistribution) {
    TerrainConfig config = Small();
    Flat(config).pixelSize = 0.5;
    terraingen::RockLayerSpec rocks;
    rocks.rocks.minDiameter = 0.5;
    rocks.rocks.ejecta.minCraterDiameter = 8.0;
    rocks.rocks.ejecta.minFreshness = 0.0;
    rocks.sizes = terraingen::Sp8023IntercraterRocksSpec{terraingen::RockModelCurve::Maximum};
    rocks.ejectaSizes = terraingen::Sp8023CraterRocksSpec{};
    config.layers.push_back(rocks);
    EXPECT_EQ(ErrorPath([&](TerrainConfig& c) { c.layers = config.layers; }), "layers[2]")
        << "the crater table starts at 0.85 m";
    rocks.extrapolate = true;
    config.layers.back() = rocks;
    const auto generated = GenerateTerrain(config);
    EXPECT_FALSE(generated.features.rocks.empty());
    for (const terraingen::RockSizesSpec& sizes :
         {terraingen::RockSizesSpec{terraingen::SynthterrainRocksSpec{}},
          terraingen::RockSizesSpec{terraingen::PowerLawSpec{0.001, 2.5}},
          terraingen::RockSizesSpec{terraingen::PiecewisePowerLawSpec{{{0.5, 0.001, 2.5}}}},
          terraingen::RockSizesSpec{terraingen::TabulatedSpec{{0.5, 10.0}, {0.01, 1e-5}}}}) {
        rocks.sizes = sizes;
        config.layers.back() = rocks;
        EXPECT_NO_THROW(GenerateTerrain(config));
    }
    rocks.sizes = terraingen::TabulatedSpec{{1.0}, {0.01}};
    config.layers.back() = rocks;
    EXPECT_EQ(ErrorPath([&](TerrainConfig& c) { c.layers = config.layers; }), "layers[2].sizes");
}

TEST(GenerateTerrain, AddsBedformsAsTheLibraryDoes) {
    TerrainConfig config = Small();
    Flat(config).pixelSize = 0.5;
    terraingen::BedformOptions ridges = terraingen::MarsTransverseAeolianRidges();
    ridges.wavelength = 5.0;
    ridges.cover = 0.5;
    ridges.fieldSize = 20.0;
    config.layers.push_back(terraingen::BedformLayerSpec{ridges});
    const auto generated = GenerateTerrain(config);
    terraingen::Terrain terrain = terraingen::BuildTerrain(config);
    terrain.layers.back() = std::make_shared<terraingen::BedformLayer>(ridges);
    ExpectSameRaster(generated.heights, terraingen::Evaluate(terrain).heights);
    ridges.leeFraction = 2.0;
    EXPECT_EQ(ErrorPath([&](TerrainConfig& c) {
                  c.layers.push_back(terraingen::BedformLayerSpec{ridges});
              }),
              "layers[2]");
}

TEST(GenerateTerrain, AddsRocksWithEjectaFromTheLastCraterLayerBeforeThem) {
    TerrainConfig config = Small();
    Flat(config).pixelSize = 0.5;
    Craters(config).minDiameter = 4.0;
    terraingen::RockLayerSpec rocks;
    rocks.rocks.minDiameter = 0.5;
    rocks.rocks.coverage = 0.03;
    rocks.rocks.ejecta.coverage = 0.2;
    rocks.rocks.ejecta.minCraterDiameter = 8.0;
    rocks.rocks.ejecta.minFreshness = 0.0;
    rocks.rocks.exposure = terraingen::ExposureParams{};
    config.layers.push_back(rocks);
    const auto generated = GenerateTerrain(config, {}, 4);
    ASSERT_FALSE(generated.features.rocks.empty());

    terraingen::Terrain terrain = terraingen::BuildTerrain(config);
    ASSERT_EQ(terrain.layers.size(), 3u);
    terraingen::RockLayerOptions options = rocks.rocks;
    terrain.layers[2] = std::make_shared<terraingen::RockLayer>(
        options, std::dynamic_pointer_cast<const terraingen::CraterLayer>(terrain.layers[1]));
    const auto composed = terraingen::Evaluate(terrain, terrain.source->Grid());
    ExpectSameRaster(generated.heights, composed.heights);
    ASSERT_EQ(generated.features.rocks.size(), composed.features.rocks.size());
    for (std::size_t n = 0; n < composed.features.rocks.size(); ++n) {
        EXPECT_EQ(generated.features.rocks[n].id, composed.features.rocks[n].id);
    }

    rocks.rocks.ejecta.minCraterDiameter = 0.0;
    config.layers.back() = rocks;
    const auto without = GenerateTerrain(config);
    EXPECT_LT(without.features.rocks.size(), generated.features.rocks.size());
}

TEST(BuildCraterModels, ShapesCratersForTheirBody) {
    CraterLayerSpec craters;
    craters.profile = terraingen::ComplexProfileSpec{};
    const auto moon = BuildCraterModels(craters);
    craters.body = terraingen::CraterBody::Mars;
    craters.density = terraingen::PowerLawSpec{0.1, 2.0};
    const auto mars = BuildCraterModels(craters);
    const terraingen::Crater crater{0.0, 0.0, 10000.0, 0.0, 1u};
    EXPECT_FALSE(moon.profile->IsComplex(10000.0));
    EXPECT_TRUE(mars.profile->IsComplex(10000.0));
    EXPECT_EQ(mars.degradation->State(crater).fresh,
              terraingen::MarsFreshCraterMorphometry(10000.0));
    EXPECT_EQ(moon.degradation->State(crater).fresh,
              terraingen::LunarFreshCraterMorphometry(10000.0));
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { Craters(c).body = terraingen::CraterBody::Mars; }),
              "layers[1].density");
    EXPECT_EQ(
        ErrorPath([](TerrainConfig& c) { Craters(c).density = terraingen::MarsSurfaceSpec{3.0}; }),
        "layers[1].density");
    TerrainConfig surface = Small();
    Craters(surface).body = terraingen::CraterBody::Mars;
    Craters(surface).density = terraingen::MarsSurfaceSpec{3.0};
    Craters(surface).minDiameter = 20.0;
    EXPECT_FALSE(GenerateTerrain(surface).features.craters.Empty());
    const auto models = BuildCraterModels(Craters(surface));
    EXPECT_NEAR(models.density->Cumulative(1000.0), terraingen::IvanovMarsChronology(3.0), 1e-15);
    EXPECT_NE(dynamic_cast<const terraingen::UniformAges*>(models.ages.get()), nullptr);
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) {
                  Craters(c).body = terraingen::CraterBody::Mars;
                  Craters(c).density = terraingen::PowerLawSpec{0.1, 2.0};
                  Craters(c).ages = terraingen::SteadyStateAgesSpec{};
              }),
              "layers[1].ages");
}

TEST(BuildCraterModels, BuildsEachSelectedModel) {
    CraterLayerSpec craters;
    auto models = BuildCraterModels(craters);
    EXPECT_NE(dynamic_cast<const terraingen::SteadyStateAges*>(models.ages.get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::FreshnessDegradation*>(models.degradation.get()),
              nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::FassettThomsonProfile*>(models.profile.get()),
              nullptr);

    craters.density = terraingen::PowerLawSpec{0.2, 2.5};
    craters.degradation = terraingen::NoDegradationSpec{};
    craters.profile = terraingen::MartinParkesDunstanSpec{};
    models = BuildCraterModels(craters);
    EXPECT_DOUBLE_EQ(models.density->Cumulative(2.0), 0.2 * std::pow(2.0, -2.5));
    EXPECT_NE(dynamic_cast<const terraingen::UniformAges*>(models.ages.get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::NoDegradation*>(models.degradation.get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::MartinParkesDunstanProfile*>(models.profile.get()),
              nullptr);

    const auto density = [&](terraingen::DensitySpec spec) {
        craters.density = spec;
        return BuildCraterModels(craters).density->Cumulative(50.0);
    };
    EXPECT_DOUBLE_EQ(density(terraingen::TraskSpec{}),
                     terraingen::TraskEquilibrium().Cumulative(50.0));
    EXPECT_DOUBLE_EQ(density(terraingen::HartmannSpec{}),
                     terraingen::HartmannEquilibrium().Cumulative(50.0));
    EXPECT_DOUBLE_EQ(
        density(terraingen::MooreSpec{terraingen::MooreTerrain::SmoothMare}),
        terraingen::MooreDistribution(terraingen::MooreTerrain::SmoothMare).Cumulative(50.0));
    EXPECT_DOUBLE_EQ(density(terraingen::ViperSpec{}),
                     terraingen::ViperSpecification().Cumulative(50.0));
    EXPECT_DOUBLE_EQ(density(terraingen::PiecewisePowerLawSpec{{{1.0, 0.1, 2.0}}}), 0.1 / 2500.0);
    EXPECT_NEAR(density(terraingen::TabulatedSpec{{0.01, 0.1}, {100.0, 1.0}, 1000.0, 1e6}),
                1e-6 * 100.0 * std::pow(5.0, -2.0), 1e-15);

    craters.ages = terraingen::FreshAgesSpec{};
    EXPECT_NE(dynamic_cast<const terraingen::FreshAges*>(BuildCraterModels(craters).ages.get()),
              nullptr);
    craters.ages = terraingen::SteadyStateAgesSpec{};
    const auto extended = BuildCraterModels(craters);
    EXPECT_GT(extended.ages->LifetimeGyr(5.0), 0.0);
    craters.ages =
        terraingen::SteadyStateAgesSpec{3.0, terraingen::EquilibriumModel::Hartmann, false};
    const auto steady = BuildCraterModels(craters);
    EXPECT_EQ(dynamic_cast<const terraingen::SteadyStateAges&>(*steady.ages).SurfaceAgeGyr(), 3.0);
    EXPECT_THROW(steady.ages->LifetimeGyr(5.0), std::domain_error);
    craters.degradation = terraingen::NoDegradationSpec{0.18};
    EXPECT_EQ(BuildCraterModels(craters).degradation->State({0, 0, 10, 0, 1}).current.depthRatio,
              0.18);
}

TEST(GenerateTerrain, MatchesComposingTheLibraryCalls) {
    TerrainConfig config = Small();
    Craters(config).manual = {{4.0, -3.0, 40.0, 0.0, 0u}};
    Craters(config).apply.datum = terraingen::CraterDatum::Plane;
    config.filters = {terraingen::RimSmoothing{.sigmaRadii = 0.05}};
    const auto terrain = GenerateTerrain(config);

    auto expected = terraingen::GenerateSyntheticTerrain(65, 49, 2.0, Noise(config).Params());
    const auto models = BuildCraterModels(Craters(config));
    CraterList manual;
    manual.Append({4.0, -3.0, 40.0, 0.0, terraingen::ManualCraterId(4.0, -3.0, 40.0), true});
    const auto bounds = terraingen::DescribeSource(config.source).Bounds();
    const auto generate = [&](const terraingen::Bounds2d& region) {
        return terraingen::GenerateCraters(
            terraingen::PlanarCells(), region, *models.density, *models.ages, 1u,
            {.dMin = 8.0, .dMax = 60.0, .marginRadii = 1.5, .manual = manual});
    };
    const CraterList craters = generate(bounds);
    const double margin = terraingen::DatumMargin(Craters(config).apply, *models.profile, 60.0);
    const terraingen::NoiseRelief noise(Noise(config).Params());
    const terraingen::HeightSampler ground = [&](double x, double y) {
        return noise.ApplyAt(0.0f, x, y, {2.0});
    };
    terraingen::ApplyCraters(expected, generate(bounds.Expanded(margin)), *models.profile,
                             *models.degradation, Craters(config).apply, 2.0, 1, ground);
    terraingen::SmoothCraterRims(expected, craters, {.sigmaRadii = 0.05});
    EXPECT_EQ(terrain.features.craters.Ids(), craters.Ids());
    ASSERT_EQ(terrain.heights.Size(), expected.Size());
    for (std::size_t i = 0; i < expected.Size(); ++i) {
        ASSERT_EQ(terrain.heights.Values()[i], expected.Values()[i]) << i;
    }
}

TEST(GenerateTerrain, GivesTheSameTerrainOnAnyNumberOfThreads) {
    TerrainConfig config = Small();
    config.filters = {terraingen::RimSmoothing{}};
    const auto serial = GenerateTerrain(config, {}, 1);
    const auto parallel = GenerateTerrain(config, {}, 4);
    ExpectSameRaster(parallel.heights, serial.heights);
    EXPECT_EQ(parallel.features.craters.Ids(), serial.features.craters.Ids());
}

TEST(GenerateTerrain, AppliesTheHurstExponentToTheGain) {
    TerrainConfig config = Small();
    config.layers.pop_back();
    Noise(config).stack[0].hurst = 0.6f;
    const auto terrain = GenerateTerrain(config);
    terraingen::SyntheticTerrainParams params;
    params.layers[0] = Noise(config).stack[0].layer;
    params.layers[0].fractal.gain = terraingen::GainFromHurst(0.6f, 2.0f);
    const auto expected = terraingen::GenerateSyntheticTerrain(65, 49, 2.0, params);
    EXPECT_TRUE(std::equal(terrain.heights.Values().begin(), terrain.heights.Values().end(),
                           expected.Values().begin()));
}

TEST(GenerateTerrain, SupportsNoLayersNoCratersAndManualCratersOnly) {
    TerrainConfig config = Small();
    Flat(config).elevation = -250.0f;
    config.layers.clear();
    const auto flat = GenerateTerrain(config);
    EXPECT_TRUE(std::all_of(flat.heights.Values().begin(), flat.heights.Values().end(),
                            [](float h) { return h == -250.0f; }));

    config = Small();
    config.layers.pop_back();
    const auto plain = GenerateTerrain(config);
    EXPECT_TRUE(plain.features.craters.Empty());
    const auto noise = terraingen::GenerateSyntheticTerrain(65, 49, 2.0, Noise(config).Params());
    EXPECT_TRUE(std::equal(plain.heights.Values().begin(), plain.heights.Values().end(),
                           noise.Values().begin()));

    CraterLayerSpec manualOnly;
    manualOnly.generate = false;
    manualOnly.manual = {{0.0, 0.0, 30.0, 0.0, 0u}, {9000.0, 0.0, 30.0, 0.0, 0u}};
    config.layers.push_back(manualOnly);
    const auto manual = GenerateTerrain(config);
    ASSERT_EQ(manual.features.craters.Size(), 1u);
    EXPECT_TRUE(manual.features.craters.At(0).manual);
    EXPECT_EQ(manual.features.craters.At(0).id, terraingen::ManualCraterId(0.0, 0.0, 30.0));
    TerrainConfig tiny;
    tiny.source = terraingen::FlatSourceSpec{1, 1, 1.0};
    EXPECT_THROW(GenerateTerrain(tiny), ConfigError);
}

TEST(GenerateTerrain, RunsLayersInOrderAndCollectsEveryLayersCraters) {
    TerrainConfig config = Small();
    CraterLayerSpec second = Craters(config);
    second.seed = 2;
    config.layers.push_back(second);
    const auto both = GenerateTerrain(config);
    config.layers.pop_back();
    const auto first = GenerateTerrain(config);
    config.layers.pop_back();
    config.layers.push_back(second);
    const auto other = GenerateTerrain(config);
    ASSERT_GT(first.features.craters.Size(), 0u);
    ASSERT_GT(other.features.craters.Size(), 0u);
    auto ids = first.features.craters.Ids();
    const auto more = other.features.craters.Ids();
    ids.insert(ids.end(), more.begin(), more.end());
    EXPECT_EQ(both.features.craters.Ids(), ids);
}

TEST(WriteCraterList, WritesCsvAndJsonExactly) {
    CraterList craters;
    craters.Append({0.1, -2.5, 12.345678901234567, 0.25, 42u, false});
    craters.Append({3.0, 4.0, 5.0, 0.0, 7u, true});
    const auto csv = TempPath("terraingen_craters.csv");
    terraingen::WriteCraterList(craters, csv, CraterListFormat::Csv);
    EXPECT_EQ(Read(csv), "x,y,diameter,age_gyr,id,manual\n"
                         "0.10000000000000001,-2.5,12.345678901234567,0.25,42,0\n"
                         "3,4,5,0,7,1\n");
    const auto json = TempPath("terraingen_craters.json");
    terraingen::WriteCraterList(craters, json, CraterListFormat::Json);
    const std::string text = Read(json);
    EXPECT_NE(text.find("\"diameter\": 12.345678901234567"), std::string::npos) << text;
    EXPECT_NE(text.find("\"manual\": true"), std::string::npos);
    EXPECT_NE(text.find("\"id\": 42"), std::string::npos);
    EXPECT_THROW(
        terraingen::WriteCraterList(craters, TempPath("missing/dir/x.csv"), CraterListFormat::Csv),
        std::runtime_error);
    const std::vector<terraingen::CraterAttributes> attributes{{2.5, 0.5, 0.25, 0.75, false},
                                                               {1.0, 0.125, 0.2, 1.0, true}};
    terraingen::WriteCraterList(craters, csv, CraterListFormat::Csv, attributes);
    EXPECT_EQ(Read(csv),
              "x,y,diameter,age_gyr,id,manual,floor_depth,rim_height,depth_ratio,"
              "freshness,complex\n"
              "0.10000000000000001,-2.5,12.345678901234567,0.25,42,0,2.5,0.5,0.25,0.75,0\n"
              "3,4,5,0,7,1,1,0.125,0.20000000000000001,1,1\n");
    terraingen::WriteCraterList(craters, json, CraterListFormat::Json, attributes);
    EXPECT_NE(Read(json).find("\"freshness\": 0.75"), std::string::npos);
    EXPECT_NE(Read(json).find("\"rim_height\": 0.125"), std::string::npos);
    EXPECT_THROW(terraingen::WriteCraterList(craters, csv, CraterListFormat::Csv, {attributes[0]}),
                 std::invalid_argument);
    if (std::filesystem::exists("/dev/full")) {
        CraterList many;
        for (std::uint64_t id = 0; id < 20000; ++id) {
            many.Append({1.0, 2.0, 3.0, 0.0, id});
        }
        EXPECT_THROW(terraingen::WriteCraterList(many, "/dev/full", CraterListFormat::Csv),
                     std::runtime_error);
    }
}

TEST(WriteOutputs, ResolvesRelativePathsAndCreatesFolders) {
    TerrainConfig config = Small();
    const auto absolute = TempPath("terraingen_absolute.csv");
    config.outputs = {CraterListOutput{"nested/list.json", CraterListFormat::Json},
                      CraterListOutput{absolute.string(), CraterListFormat::Csv}};
    const auto terrain = GenerateTerrain(config);
    const auto directory = TempPath("terraingen_outputs");
    std::filesystem::remove_all(directory);
    const auto written = terraingen::WriteOutputs(config, terrain, directory);
    ASSERT_EQ(written.size(), 2u);
    EXPECT_EQ(written[0], directory / "nested/list.json");
    EXPECT_EQ(written[1], absolute);
    EXPECT_TRUE(std::filesystem::exists(written[0]));
    const std::string csv = Read(absolute);
    EXPECT_EQ(std::count(csv.begin(), csv.end(), '\n'),
              static_cast<long>(terrain.features.craters.Size() + 1));
}

TEST(WriteOutputs, WritesRockListsAndBoulderModels) {
    TerrainConfig config = Small();
    Flat(config) = {200, 200, 0.5};
    terraingen::RockLayerSpec rocks;
    rocks.rocks.coverage = 0.3;
    rocks.rocks.minDiameter = 0.5;
    rocks.rocks.relief = terraingen::RockRelief::Shape;
    rocks.rocks.models.minDiameter = 1.5;
    rocks.rocks.shapes.subdivisions = 1;
    config.layers.push_back(rocks);
    config.outputs = {terraingen::RockListOutput{"rocks.csv"},
                      terraingen::RockListOutput{"rocks.json", CraterListFormat::Json},
                      terraingen::BoulderModelsOutput{"boulders.glb"},
                      terraingen::BoulderModelsOutput{"boulders.ply"}};
    const auto terrain = GenerateTerrain(config);
    ASSERT_FALSE(terrain.features.boulders.empty());
    for (const auto& boulder : terrain.features.boulders) {
        ASSERT_TRUE(boulder.rock.ground.has_value());
        EXPECT_GE(boulder.rock.diameter, 1.5);
    }
    const auto directory = TempPath("terraingen_rock_outputs");
    std::filesystem::remove_all(directory);
    const auto written = terraingen::WriteOutputs(config, terrain, directory);
    ASSERT_EQ(written.size(), 4u);
    const std::string csv = Read(written[0]);
    EXPECT_EQ(csv.rfind("x,y,ground,diameter,height,burial,yaw,lean,id,model\n", 0), 0u);
    EXPECT_EQ(std::count(csv.begin(), csv.end(), '\n'),
              static_cast<long>(terrain.features.rocks.size() + 1));
    EXPECT_NE(Read(written[1]).find("\"model\": true"), std::string::npos);
    EXPECT_EQ(Read(written[2]).substr(0, 4), "glTF");
    EXPECT_EQ(Read(written[3]).substr(0, 3), "ply");

    terraingen::RockLayerSpec none;
    none.rocks.minDiameter = 0.5;
    config.layers.back() = none;
    config.outputs = {terraingen::BoulderModelsOutput{"empty.glb"}};
    EXPECT_TRUE(terraingen::WriteOutputs(config, GenerateTerrain(config), directory).empty())
        << "nothing to write without models";

    EXPECT_EQ(ErrorPath([](TerrainConfig& c) {
                  c.outputs = {terraingen::BoulderModelsOutput{"boulders.obj"}};
              }),
              "outputs[0].path");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) {
                  c.outputs = {terraingen::BoulderModelsOutput{"boulders.glb", 200.0}};
              }),
              "outputs[0].crease_angle");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { c.outputs = {terraingen::RockListOutput{}}; }),
              "outputs[0].path");
    EXPECT_THROW(terraingen::WriteBoulderModels({}, directory / "x.glb"), std::invalid_argument);
    EXPECT_THROW(terraingen::WriteBoulderModels(terrain.features.boulders, directory / "x.obj"),
                 std::invalid_argument);
    EXPECT_THROW(
        terraingen::WriteBoulderModels(terrain.features.boulders, directory / "x.glb", -1.0),
        std::invalid_argument);
    EXPECT_THROW(
        terraingen::WriteRockList({}, TempPath("missing/dir/r.csv"), CraterListFormat::Csv),
        std::runtime_error);
}

namespace {

/// GeoKeys for a projected (or geographic) CRS with pixel-is-area rasters.
terraingen::GeoKeys Keys(bool geographic = false) {
    return {{1, 1, 0, 2, 1024, 0, 1, static_cast<std::uint16_t>(geographic ? 2 : 1), 1025, 0, 1, 1},
            {},
            {}};
}

/// Writes a 40 x 30 raster of 5 m cells, origin (1000, 2000), to `folder`/`name`.
terraingen::Raster<float> WriteDem(const std::filesystem::path& folder, const std::string& name,
                                   const terraingen::GeoKeys& keys = Keys()) {
    terraingen::Raster<float> dem(40, 30);
    dem.SetTransform(terraingen::GeoTransform::NorthUp(1000.0, 2000.0, 5.0));
    for (std::size_t row = 0; row < dem.Height(); ++row) {
        for (std::size_t col = 0; col < dem.Width(); ++col) {
            dem(col, row) = static_cast<float>(10.0 * std::sin(0.2 * static_cast<double>(col)) +
                                               0.5 * static_cast<double>(row));
        }
    }
    std::filesystem::create_directories(folder);
    terraingen::WriteGeoTiff(folder / name, dem, keys);
    return dem;
}

TerrainConfig RasterConfig(const std::string& path) {
    TerrainConfig config;
    config.source =
        terraingen::RasterSourceSpec{path, 0, std::nullopt, terraingen::ResampleMethod::Cubic};
    return config;
}

terraingen::RasterSourceSpec& RasterSpec(TerrainConfig& config) {
    return std::get<terraingen::RasterSourceSpec>(config.source);
}

}  // namespace

TEST(DescribeSource, ReadsTheRasterHeaderAndResampledSize) {
    const auto folder = TempPath("terraingen_describe");
    WriteDem(folder, "dem.tif");
    TerrainConfig config = RasterConfig("dem.tif");
    const auto grid = terraingen::DescribeSource(config.source, folder);
    EXPECT_EQ(grid.width, 40u);
    EXPECT_EQ(grid.height, 30u);
    EXPECT_EQ(grid.transform, terraingen::GeoTransform::NorthUp(1000.0, 2000.0, 5.0));
    EXPECT_DOUBLE_EQ(grid.PixelSize(), 5.0);
    EXPECT_DOUBLE_EQ(grid.Area(), 200.0 * 150.0);
    const Bounds2d bounds = grid.Bounds();
    EXPECT_EQ(bounds.minX, 1000.0);
    EXPECT_EQ(bounds.maxX, 1200.0);
    EXPECT_EQ(bounds.minY, 1850.0);
    EXPECT_EQ(bounds.maxY, 2000.0);
    RasterSpec(config).pixelSize = 2.5;
    const auto fine = terraingen::DescribeSource(config.source, folder);
    EXPECT_EQ(fine.width, 80u);
    EXPECT_EQ(fine.height, 60u);
    EXPECT_DOUBLE_EQ(fine.PixelSize(), 2.5);
}

TEST(ValidateConfig, ReportsRasterSourceProblems) {
    const auto folder = TempPath("terraingen_raster_errors");
    WriteDem(folder, "dem.tif");
    WriteDem(folder, "degrees.tif", Keys(true));
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
    EXPECT_EQ(error([](TerrainConfig& c) {
                  RasterSpec(c).pixelSize = -1.0;
              }).rfind("source.pixel_size", 0),
              0u);
    EXPECT_EQ(error([](TerrainConfig& c) {
                  RasterSpec(c).pixelSize = 1e-6;
              }).rfind("source.pixel_size", 0),
              0u);
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
    terraingen::WriteGeoTiff(folder / "rotated.tif", rotated, Keys());
    EXPECT_EQ(error([](TerrainConfig& c) {
                  RasterSpec(c).path = "rotated.tif";
                  RasterSpec(c).pixelSize = 0.5;
              }).rfind("source.pixel_size", 0),
              0u);
    terraingen::WriteGeoTiff(folder / "tiny.tif", terraingen::Raster<float>(1, 1, 0.0f), Keys());
    EXPECT_EQ(
        error([](TerrainConfig& c) { RasterSpec(c).path = "tiny.tif"; }).rfind("source.path", 0),
        0u);
}

TEST(GenerateTerrain, LoadsResamplesAndDetailsARasterSource) {
    const auto folder = TempPath("terraingen_raster_generate");
    const auto dem = WriteDem(folder, "dem.tif");
    TerrainConfig config = RasterConfig("dem.tif");
    const auto plain = GenerateTerrain(config, folder);
    ExpectSameRaster(plain.heights, dem);
    EXPECT_EQ(plain.geoKeys, Keys());
    EXPECT_EQ(plain.heights.Transform(), dem.Transform());

    RasterSpec(config).scale = 1000.0;
    RasterSpec(config).offset = -5.0;
    const auto scaled = GenerateTerrain(config, folder);
    for (std::size_t i = 0; i < dem.Size(); ++i) {
        ASSERT_EQ(scaled.heights.Values()[i],
                  static_cast<float>(static_cast<double>(dem.Values()[i]) * 1000.0 - 5.0));
    }
    RasterSpec(config).scale.reset();
    RasterSpec(config).offset.reset();

    RasterSpec(config).pixelSize = 2.5;
    RasterSpec(config).resampling = terraingen::ResampleMethod::Bilinear;
    const auto resampled = GenerateTerrain(config, folder);
    ExpectSameRaster(resampled.heights,
                     terraingen::Resample(dem, 2.5, terraingen::ResampleMethod::Bilinear));

    RasterSpec(config).pixelSize.reset();
    terraingen::NoiseLayerConfig detail;
    detail.layer.fractal.wavelength = 20.0f;
    detail.layer.fractal.amplitude = 1.5f;
    terraingen::NoiseLayerSpec detailLayer;
    detailLayer.stack = {detail};
    config.layers = {detailLayer};
    const auto detailed = GenerateTerrain(config, folder);
    const terraingen::NoiseLayer layer = detail.Effective();
    for (std::size_t row = 0; row < dem.Height(); ++row) {
        for (std::size_t col = 0; col < dem.Width(); ++col) {
            const auto centre = dem.CellCenter(col, row);
            const float noise =
                terraingen::EvaluateNoiseLayers(static_cast<float>(centre.x - 1000.0),
                                                static_cast<float>(centre.y - 2000.0), &layer, 1);
            ASSERT_EQ(detailed.heights(col, row), dem(col, row) + noise) << col << " " << row;
        }
    }
}

TEST(GenerateTerrain, PlacesCratersInTheRastersCoordinates) {
    const auto folder = TempPath("terraingen_raster_craters");
    WriteDem(folder, "dem.tif");
    TerrainConfig config = RasterConfig("dem.tif");
    CraterLayerSpec craters;
    craters.density = terraingen::PowerLawSpec{0.1, 2.0};
    craters.minDiameter = 15.0;
    craters.maxDiameter = 60.0;
    craters.manual = {{1100.0, 1925.0, 50.0, 0.0, 0u, true}};
    config.layers = {craters};
    const auto terrain = GenerateTerrain(config, folder);
    ASSERT_GT(terrain.features.craters.Size(), 2u);
    const Bounds2d bounds{1000.0, 1850.0, 1200.0, 2000.0};
    for (std::size_t n = 0; n < terrain.features.craters.Size(); ++n) {
        const auto crater = terrain.features.craters.At(n);
        EXPECT_TRUE(bounds.Expanded(0.75 * crater.diameter).Contains(crater.x, crater.y)) << n;
    }
    EXPECT_LT(terrain.heights(20, 15), WriteDem(folder, "again.tif")(20, 15) - 2.0f);
}

TEST(WriteOutputs, WritesHeightmapsWithTheSourceProjection) {
    const auto folder = TempPath("terraingen_heightmap");
    const auto dem = WriteDem(folder, "dem.tif");
    TerrainConfig config = RasterConfig("dem.tif");
    config.outputs = {
        terraingen::HeightmapOutput{"out/height.tif", terraingen::TiffCompression::Deflate, 16}};
    const auto terrain = GenerateTerrain(config, folder);
    const auto written = terraingen::WriteOutputs(config, terrain, folder);
    ASSERT_EQ(written.size(), 1u);
    const auto info = terraingen::ReadGeoTiffInfo(written[0]);
    EXPECT_EQ(info.compression, terraingen::TiffCompression::Deflate);
    const auto read = terraingen::ReadGeoTiff(written[0]);
    ExpectSameRaster(read.raster, dem);
    EXPECT_EQ(read.geoKeys, Keys());
    EXPECT_EQ(read.raster.Transform(), dem.Transform());
}

TEST(GenerateTerrain, FillsARastersGapsFromTheSourcesChainedAfterIt) {
    const auto folder = TempPath("terraingen_chain");
    const auto dem = WriteDem(folder, "dem.tif");
    auto holes = dem;
    holes.SetNoData(-9999.0f);
    for (std::size_t row = 10; row < 20; ++row) {
        for (std::size_t col = 5; col < 12; ++col) {
            holes(col, row) = -9999.0f;
        }
    }
    terraingen::WriteGeoTiff(folder / "holes.tif", holes, Keys());
    const auto raster = [](const std::string& path) {
        return terraingen::RasterSourceSpec{path, 0, std::nullopt,
                                            terraingen::ResampleMethod::Cubic};
    };
    TerrainConfig config;
    config.source = terraingen::ChainSourceSpec{{raster("holes.tif"), raster("dem.tif")}};
    const auto filled = GenerateTerrain(config, folder);
    ExpectSameRaster(filled.heights, dem);
    EXPECT_EQ(filled.geoKeys, Keys());

    config.source = terraingen::ChainSourceSpec{
        {raster("holes.tif"), terraingen::FlatSourceSpec{2, 2, 1.0, 3.5f}}};
    const auto datum = GenerateTerrain(config, folder);
    EXPECT_EQ(datum.heights(6, 12), 3.5f);
    EXPECT_EQ(datum.heights(30, 2), dem(30, 2));

    const auto error = [&](terraingen::ChainSourceSpec chain) {
        TerrainConfig bad;
        bad.source = std::move(chain);
        try {
            ValidateConfig(bad, folder);
        } catch (const ConfigError& e) {
            return e.Path();
        }
        return std::string("valid");
    };
    EXPECT_EQ(error({}), "source.sources");
    EXPECT_EQ(error({{raster("dem.tif"), raster("missing.tif")}}), "source.sources[1].path");
    EXPECT_EQ(error({{terraingen::ChainSourceSpec{{terraingen::FlatSourceSpec{1, 1, 1.0}}}}}),
              "source.sources[0].sources[0].width");
}

namespace {

/// `Small` with a synthetic albedo and bright crater material from its crater layer.
TerrainConfig WithAlbedo() {
    TerrainConfig config = Small();
    Craters(config).manual = {{10.0, -8.0, 30.0, 0.0, 77u, true}};
    terraingen::SyntheticAlbedoSpec synthetic;
    synthetic.rgb = true;
    synthetic.saturation = 0.05;
    synthetic.valueNoise = {terraingen::NoiseLayerConfig{}};
    synthetic.valueNoise[0].layer.fractal.amplitude = 0.2f;
    synthetic.valueNoise[0].layer.fractal.wavelength = 40.0f;
    config.albedo = terraingen::AlbedoSpec{synthetic, {terraingen::CraterAlbedoSpec{}}};
    return config;
}

}  // namespace

TEST(ValidateConfig, ReportsAlbedoProblemsByPath) {
    const auto error = [](auto mutate) {
        TerrainConfig config = WithAlbedo();
        mutate(config);
        try {
            ValidateConfig(config);
        } catch (const ConfigError& e) {
            return e.Path();
        }
        return std::string("valid");
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
    TerrainConfig none = Small();
    EXPECT_THROW(terraingen::BuildAlbedo(none, terraingen::BuildTerrain(none)), ConfigError);
}

TEST(GenerateTerrain, EvaluatesTheAlbedoAsTheLibraryDoes) {
    const TerrainConfig config = WithAlbedo();
    const auto generated = GenerateTerrain(config, {}, 3);
    ASSERT_EQ(generated.albedo.size(), 3u);

    const terraingen::Terrain terrain = terraingen::BuildTerrain(config);
    const auto craters =
        std::dynamic_pointer_cast<const terraingen::CraterLayer>(terrain.layers[1]);
    ASSERT_TRUE(craters);
    const auto& synthetic = std::get<terraingen::SyntheticAlbedoSpec>(config.albedo->source);
    const terraingen::Albedo albedo{std::make_shared<terraingen::SyntheticAlbedo>(
                                        synthetic.Params(), terrain.source->ReferencePoint()),
                                    {std::make_shared<terraingen::CraterAlbedo>(craters)}};
    const auto grid = terraingen::RasterGrid::Of(generated.heights);
    const auto expected = terraingen::EvaluateAlbedo(albedo, grid, {.spacing = grid.PixelSize()});
    for (std::size_t band = 0; band < 3; ++band) {
        ExpectSameRaster(generated.albedo[band], expected[band]);
    }
    const terraingen::Albedo plain{albedo.source, {}};
    const auto background = terraingen::EvaluateAlbedo(plain, grid, {.spacing = grid.PixelSize()});
    const auto centre = generated.albedo[0].Transform().WorldToPixel(10.0, -8.0);
    const auto col = static_cast<std::size_t>(centre.x);
    const auto row = static_cast<std::size_t>(centre.y);
    EXPECT_GT(generated.albedo[0](col, row), background[0](col, row));

    TerrainConfig both = WithAlbedo();
    both.layers.push_back(Craters(both));
    std::get<terraingen::CraterAlbedoSpec>(both.albedo->layers[0]).craterLayer.reset();
    EXPECT_EQ(terraingen::BuildAlbedo(both, terraingen::BuildTerrain(both)).layers.size(), 2u);
    std::get<terraingen::CraterAlbedoSpec>(both.albedo->layers[0]).craterLayer = 1;
    EXPECT_EQ(terraingen::BuildAlbedo(both, terraingen::BuildTerrain(both)).layers.size(), 1u);
    EXPECT_TRUE(GenerateTerrain(Small()).albedo.empty());
}

TEST(WriteOutputs, WritesARasterAlbedoUpscaledWithTheSourceProjection) {
    const auto folder = TempPath("terraingen_albedo");
    WriteDem(folder, "dem.tif");
    terraingen::Raster<float> map(10, 8);
    map.SetTransform(terraingen::GeoTransform::NorthUp(1000.0, 2000.0, 20.0));
    for (std::size_t i = 0; i < map.Size(); ++i) {
        map.Values()[i] = static_cast<float>(1000 + 37 * (i % 11));
    }
    terraingen::WriteGeoTiff(folder / "map.tif", map, Keys());
    TerrainConfig config = RasterConfig("dem.tif");
    terraingen::RasterAlbedoSpec raster{"map.tif", {0}, 0.0001, 0.0, {}};
    raster.options.detail.enabled = false;
    config.albedo = terraingen::AlbedoSpec{raster, {}};
    config.outputs = {terraingen::AlbedoOutput{"out/albedo.tif"}};
    const auto terrain = GenerateTerrain(config, folder);
    const auto written = terraingen::WriteOutputs(config, terrain, folder);
    ASSERT_EQ(written.size(), 1u);
    const auto read = terraingen::ReadGeoTiff(written[0]);
    EXPECT_EQ(read.geoKeys, Keys());
    EXPECT_EQ(read.raster.Transform(), terrain.heights.Transform());
    const auto centre = read.raster.CellCenter(3, 4);
    const float sampled =
        terraingen::SampleRaster(map, centre.x, centre.y, terraingen::ResampleMethod::Cubic);
    EXPECT_NEAR(read.raster(3, 4), 0.0001f * sampled, 1e-6f);
    float lowest = 1.0f;
    float highest = 0.0f;
    for (const float value : read.raster.Values()) {
        lowest = std::min(lowest, value);
        highest = std::max(highest, value);
    }
    EXPECT_GT(lowest, 0.09f);
    EXPECT_LT(highest, 0.15f);

    const auto rejected = [&](terraingen::RasterAlbedoSpec spec) {
        TerrainConfig bad = config;
        bad.albedo->source = std::move(spec);
        try {
            ValidateConfig(bad, folder);
        } catch (const ConfigError& error) {
            return error.Path();
        }
        return std::string("valid");
    };
    EXPECT_EQ(rejected({"map.tif", {1}, {}, {}, {}}), "albedo.source.bands");
    EXPECT_EQ(rejected({"map.tif", {0}, NAN, {}, {}}), "albedo.source.scale");
    terraingen::RasterAlbedoSpec detail{"map.tif", {0}, {}, {}, {}};
    detail.options.detail.octaves = 0;
    EXPECT_EQ(rejected(detail), "albedo.source.detail");
    terraingen::Raster<float> rotated = map;
    rotated.SetTransform({1000.0, 20.0, 1.0, 2000.0, 0.0, -20.0});
    terraingen::WriteGeoTiff(folder / "rotated.tif", rotated, Keys());
    EXPECT_EQ(rejected({"rotated.tif", {0}, {}, {}, {}}), "albedo.source.path");

    config.albedo->source = terraingen::RasterAlbedoSpec{"map.tif", {0}, {}, {}, {}};
    EXPECT_NEAR(GenerateTerrain(config, folder).albedo[0](3, 4), sampled, 1e-3f);
    config.albedo.reset();
    EXPECT_THROW(terraingen::WriteOutputs(config, GenerateTerrain(Small()), folder),
                 std::runtime_error);
}

namespace {

/// A small configuration on a lunar-sized sphere: a flat source at (30, -45), noise and craters.
TerrainConfig OnASphere() {
    TerrainConfig config = Small();
    config.surface = terraingen::SphereSurfaceSpec{1737400.0};
    Flat(config).centreX = 30.0;
    Flat(config).centreY = -45.0;
    Flat(config).pixelSize = 4.0;
    return config;
}

}  // namespace

TEST(ValidateConfig, ReportsSphereProblemsByPath) {
    const auto error = [](auto mutate) {
        TerrainConfig config = OnASphere();
        mutate(config);
        try {
            ValidateConfig(config);
        } catch (const ConfigError& e) {
            return e.Path();
        }
        return std::string("valid");
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
        "outputs[0]");
    EXPECT_EQ(error([](TerrainConfig& c) {
                  terraingen::SyntheticAlbedoSpec synthetic;
                  synthetic.valueNoise = {terraingen::NoiseLayerConfig{}};
                  synthetic.valueNoise[0].layer.fractal.basis = terraingen::NoiseBasis::WorleyF1;
                  c.albedo = terraingen::AlbedoSpec{synthetic, {}};
              }),
              "albedo.source.value_noise[0].basis");
    EXPECT_THROW(terraingen::BuildSurface(terraingen::SphereSurfaceSpec{-1.0}), ConfigError);
}

TEST(GenerateTerrain, BuildsSphereTerrainsAndWritesGeographicRasters) {
    TerrainConfig config = OnASphere();
    terraingen::SyntheticAlbedoSpec synthetic;
    config.albedo = terraingen::AlbedoSpec{synthetic, {terraingen::CraterAlbedoSpec{}}};
    config.filters = {terraingen::RimSmoothing{}};
    config.outputs = {terraingen::HeightmapOutput{"height.tif"},
                      terraingen::AlbedoOutput{"albedo.tif"},
                      CraterListOutput{"craters.csv", CraterListFormat::Csv}};
    const auto generated = GenerateTerrain(config, {}, 2);
    const auto grid = terraingen::RasterGrid::Of(generated.heights);
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    EXPECT_NEAR(grid.Spacing(moon), 4.0, 1e-9);
    EXPECT_NEAR(grid.Bounds().minX + grid.Bounds().maxX, 60.0, 1e-9);
    ASSERT_GT(generated.features.craters.Size(), 0u);
    for (std::size_t n = 0; n < generated.features.craters.Size(); ++n) {
        EXPECT_NEAR(generated.features.craters.X()[n], 30.0, 0.1);
        EXPECT_NEAR(generated.features.craters.Y()[n], -45.0, 0.1);
    }
    EXPECT_EQ(generated.geoKeys, terraingen::GeographicSphereKeys(1737400.0));
    EXPECT_EQ(generated.albedo.size(), 1u);

    const terraingen::Terrain terrain = terraingen::BuildTerrain(config);
    EXPECT_EQ(terrain.surface, moon);
    terraingen::Terrain layersOnly = terrain;
    layersOnly.filters.clear();
    const auto unfiltered = terraingen::Evaluate(layersOnly, grid);
    const auto centre = unfiltered.heights.CellCenter(20, 30);
    EXPECT_EQ(terraingen::EvaluateAt(layersOnly, centre.x, centre.y, 4.0),
              unfiltered.heights(20, 30));

    const auto directory = TempPath("terraingen_sphere");
    std::filesystem::remove_all(directory);
    const auto written = terraingen::WriteOutputs(config, generated, directory);
    ASSERT_EQ(written.size(), 3u);
    const auto read = terraingen::ReadGeoTiff(written[0]);
    EXPECT_TRUE(read.geoKeys.IsGeographic());
    EXPECT_EQ(read.raster.Transform(), generated.heights.Transform());
    EXPECT_TRUE(terraingen::ReadGeoTiffInfo(written[1]).geoKeys.IsGeographic());
}

TEST(GenerateTerrain, WearsCratersByDiffusionAsTheLibraryDoes) {
    TerrainConfig config = Small();
    Craters(config).degradation = terraingen::DiffusionDegradationSpec{};
    Craters(config).profile =
        terraingen::FassettThomsonSpec{terraingen::FassettThomsonRim::Plateau};
    const auto generated = GenerateTerrain(config, {}, 2);
    const CraterLayerSpec& spec = Craters(config);
    const terraingen::CraterModels models = BuildCraterModels(spec);
    const auto* profile = dynamic_cast<const terraingen::DiffusedProfile*>(models.profile.get());
    ASSERT_NE(profile, nullptr);
    ASSERT_GT(generated.features.craters.Size(), 0u);
    const terraingen::Crater crater = generated.features.craters.At(0);
    const terraingen::CraterState state = models.degradation->State(crater);
    EXPECT_DOUBLE_EQ(state.diffusion,
                     terraingen::Fassett2022HartmannDiffusivity()(crater.diameter, crater.ageGyr) /
                         (crater.diameter * crater.diameter));

    const auto error = [](auto mutate) {
        TerrainConfig bad = Small();
        terraingen::DiffusionDegradationSpec diffusion;
        mutate(diffusion, Craters(bad));
        Craters(bad).degradation = diffusion;
        try {
            ValidateConfig(bad);
        } catch (const ConfigError& e) {
            return e.Path();
        }
        return std::string("valid");
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
    for (const auto model : {terraingen::DiffusivityModel::Fassett2022Trask,
                             terraingen::DiffusivityModel::FassettThomson2014,
                             terraingen::DiffusivityModel::FassettThomson2014Calibration}) {
        CraterLayerSpec layer = Craters(config);
        terraingen::DiffusionDegradationSpec diffusion;
        diffusion.diffusivity = model;
        layer.degradation = diffusion;
        const auto built = BuildCraterModels(layer);
        EXPECT_GT(built.degradation->State({0.0, 0.0, 500.0, 1.0, 1u}).diffusion, 0.0);
    }
}
