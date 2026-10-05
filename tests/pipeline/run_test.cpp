#include "terraingen/pipeline/run.h"

#include "terraingen/craters.h"
#include "terraingen/pipeline/config_json.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using terraingen::BuildCraterModels;
using terraingen::ConfigError;
using terraingen::CraterConfig;
using terraingen::CraterList;
using terraingen::CraterListFormat;
using terraingen::CraterListOutput;
using terraingen::GenerateTerrain;
using terraingen::TerrainConfig;
using terraingen::ValidateConfig;

namespace {

/// A small, fast configuration.
TerrainConfig Small() {
    TerrainConfig config;
    config.grid = {65, 49, 2.0};
    config.surface.layers[0].layer.fractal.wavelength = 200.0f;
    config.surface.layers[0].layer.fractal.amplitude = 10.0f;
    config.craters->minDiameter = 8.0;
    config.craters->maxDiameter = 60.0;
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

std::filesystem::path TempPath(const std::string& name) {
    return std::filesystem::path(::testing::TempDir()) / name;
}

}  // namespace

TEST(ValidateConfig, AcceptsTheDefaultsAndReportsEachProblemByPath) {
    EXPECT_NO_THROW(ValidateConfig(TerrainConfig{}));
    EXPECT_EQ(ErrorPath([](TerrainConfig&) {}), "valid");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { c.schemaVersion = 7; }), "schema_version");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { c.grid.width = 1; }), "grid.width");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { c.grid.height = 100000; }), "grid.height");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { c.grid.pixelSize = 0.0; }), "grid.pixel_size");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { c.surface.layers.clear(); }), "surface.layers");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { c.surface.layers[0].layer.fractal.octaves = 0; }),
              "surface.layers[0]");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { c.surface.layers[0].hurst = 1.0f / 0.0f; }),
              "surface.layers[0].hurst");
    EXPECT_EQ(
        ErrorPath([](TerrainConfig& c) { c.craters->density = terraingen::LunarSurfaceSpec{9.0}; }),
        "craters.density");
    EXPECT_EQ(
        ErrorPath([](TerrainConfig& c) { c.craters->ages = terraingen::UniformAgesSpec{0.0}; }),
        "craters.ages");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) {
                  c.craters->degradation = terraingen::FreshnessDegradationSpec{0.5};
              }),
              "craters.degradation");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { c.craters->spacing.diameterRatio = 0.5; }),
              "craters.spacing");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { c.craters->apply.interiorReplacement = 2.0; }),
              "craters.apply");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { c.craters->rimSmoothing.strength = -1.0; }),
              "craters.rim_smoothing");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { c.craters->minDiameter = 0.0; }),
              "craters.min_diameter");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { c.craters->maxDiameter = 5.0; }),
              "craters.max_diameter");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { c.craters->minDiameter = 2.0; }),
              "craters.min_diameter");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) {
                  c.craters->density = terraingen::TraskSpec{};
                  c.craters->minDiameter = 0.001;
              }),
              "craters.min_diameter");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { c.craters->manual = {{0.0, 0.0, -1.0, 0.0}}; }),
              "craters.manual[0].diameter");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { c.craters->manual = {{0.0, 0.0, 5.0, -1.0}}; }),
              "craters.manual[0].age_gyr");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { c.craters->manual = {{1.0 / 0.0, 0.0, 5.0, 0.0}}; }),
              "craters.manual[0]");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) {
                  c.craters->generate = false;
                  c.craters->minDiameter = -1.0;
              }),
              "valid");
    EXPECT_EQ(ErrorPath([](TerrainConfig& c) { c.outputs = {CraterListOutput{}}; }),
              "outputs[0].path");
}

TEST(BuildCraterModels, BuildsEachSelectedModel) {
    CraterConfig craters;
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
    config.craters->manual = {{4.0, -3.0, 40.0, 0.0, 0u}};
    config.craters->apply.datum = terraingen::CraterDatum::Plane;
    config.craters->rimSmoothing.sigmaRadii = 0.05;
    const auto terrain = GenerateTerrain(config);

    auto expected = terraingen::GenerateSyntheticTerrain(65, 49, 2.0, config.surface.Params());
    const auto models = BuildCraterModels(*config.craters);
    CraterList manual;
    manual.Append({4.0, -3.0, 40.0, 0.0, terraingen::ManualCraterId(4.0, -3.0, 40.0), true});
    const CraterList craters = terraingen::GenerateCraters(
        terraingen::PlanarCells(), terraingen::GridBounds(config.grid), *models.density,
        *models.ages, 1u, {.dMin = 8.0, .dMax = 60.0, .marginRadii = 1.5, .manual = manual});
    terraingen::ApplyCraters(expected, craters, *models.profile, *models.degradation,
                             config.craters->apply);
    terraingen::SmoothCraterRims(expected, craters, config.craters->rimSmoothing);
    EXPECT_EQ(terrain.craters.Ids(), craters.Ids());
    ASSERT_EQ(terrain.heights.Size(), expected.Size());
    for (std::size_t i = 0; i < expected.Size(); ++i) {
        ASSERT_EQ(terrain.heights.Values()[i], expected.Values()[i]) << i;
    }
}

TEST(GenerateTerrain, AppliesTheHurstExponentToTheGain) {
    TerrainConfig config = Small();
    config.craters.reset();
    config.surface.layers[0].hurst = 0.6f;
    const auto terrain = GenerateTerrain(config);
    terraingen::SyntheticTerrainParams params;
    params.layers[0] = config.surface.layers[0].layer;
    params.layers[0].fractal.gain = terraingen::GainFromHurst(0.6f, 2.0f);
    const auto expected = terraingen::GenerateSyntheticTerrain(65, 49, 2.0, params);
    EXPECT_TRUE(std::equal(terrain.heights.Values().begin(), terrain.heights.Values().end(),
                           expected.Values().begin()));
}

TEST(GenerateTerrain, SupportsNoCratersAndManualCratersOnly) {
    TerrainConfig config = Small();
    config.craters.reset();
    const auto plain = GenerateTerrain(config);
    EXPECT_TRUE(plain.craters.Empty());
    const auto noise = terraingen::GenerateSyntheticTerrain(65, 49, 2.0, config.surface.Params());
    EXPECT_TRUE(std::equal(plain.heights.Values().begin(), plain.heights.Values().end(),
                           noise.Values().begin()));

    config.craters = CraterConfig{};
    config.craters->generate = false;
    config.craters->manual = {{0.0, 0.0, 30.0, 0.0, 0u}, {9000.0, 0.0, 30.0, 0.0, 0u}};
    const auto manual = GenerateTerrain(config);
    ASSERT_EQ(manual.craters.Size(), 1u);
    EXPECT_TRUE(manual.craters.At(0).manual);
    TerrainConfig tiny;
    tiny.grid = {1, 1, 1.0};
    EXPECT_THROW(GenerateTerrain(tiny), ConfigError);
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
              static_cast<long>(terrain.craters.Size() + 1));
}
