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
    const CraterList craters = terraingen::GenerateCraters(
        terraingen::PlanarCells(), terraingen::DescribeSource(config.source).Bounds(),
        *models.density, *models.ages, 1u,
        {.dMin = 8.0, .dMax = 60.0, .marginRadii = 1.5, .manual = manual});
    terraingen::ApplyCraters(expected, craters, *models.profile, *models.degradation,
                             Craters(config).apply);
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
