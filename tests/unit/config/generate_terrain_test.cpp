/// @file
/// @brief Tests of `GenerateTerrain` and `Summary` (pipeline/run.cpp).
#include "configs.h"
#include "support/raster_equality.h"
#include "support/temp_path.h"
#include "terraingen/config/generate.h"
#include "terraingen/raster/resample.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

using terraingen::BuildCraterModels;
using terraingen::ConfigError;
using terraingen::CraterLayerSpec;
using terraingen::CraterList;
using terraingen::GenerateTerrain;
using terraingen::TerrainConfig;
using terraingen::testing::Craters;
using terraingen::testing::ExpectSameRaster;
using terraingen::testing::Flat;
using terraingen::testing::Keys;
using terraingen::testing::Noise;
using terraingen::testing::RasterConfig;
using terraingen::testing::RasterSpec;
using terraingen::testing::Small;
using terraingen::testing::TempPath;
using terraingen::testing::WriteDem;

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
    terraingen::ApplyCraters(expected, generate(bounds.Expanded(margin)), models.Shape(),
                             Craters(config).apply, {2.0, 1, ground});
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
    EXPECT_TRUE(manual.features.craters.At(0).manual) << "building marks manual craters";
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

TEST(GenerateTerrain, AddsRoughnessAsTheLibraryDoes) {
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

TEST(GenerateTerrain, DrawsBouldersFromEachKindOfSizeDistribution) {
    TerrainConfig config = Small();
    Flat(config).pixelSize = 0.5;
    terraingen::BoulderLayerSpec boulders;
    boulders.boulders.sizes.minDiameter = 0.5;
    boulders.boulders.ejecta.minCraterDiameter = 8.0;
    boulders.boulders.ejecta.minFreshness = 0.0;
    boulders.sizes =
        terraingen::Sp8023IntercraterBouldersSpec{terraingen::SpecificationCurve::Maximum};
    boulders.ejectaSizes = terraingen::Sp8023CraterBouldersSpec{};
    boulders.extrapolate = true;
    config.layers.push_back(boulders);
    const auto generated = GenerateTerrain(config);
    EXPECT_FALSE(generated.features.boulders.empty());
    for (const terraingen::BoulderSizesSpec& sizes :
         {terraingen::BoulderSizesSpec{terraingen::SynthterrainBouldersSpec{}},
          terraingen::BoulderSizesSpec{terraingen::PowerLawSpec{0.001, 2.5}},
          terraingen::BoulderSizesSpec{terraingen::PiecewisePowerLawSpec{{{0.5, 0.001, 2.5}}}},
          terraingen::BoulderSizesSpec{terraingen::TabulatedSpec{{0.5, 10.0}, {0.01, 1e-5}}}}) {
        boulders.sizes = sizes;
        config.layers.back() = boulders;
        EXPECT_NO_THROW(GenerateTerrain(config));
    }
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
}

TEST(GenerateTerrain, AddsBouldersWithEjectaFromTheLastCraterLayerBeforeThem) {
    TerrainConfig config = Small();
    Flat(config).pixelSize = 0.5;
    Craters(config).minDiameter = 4.0;
    terraingen::BoulderLayerSpec boulders;
    boulders.boulders.sizes.minDiameter = 0.5;
    boulders.boulders.sizes.coverage = 0.03;
    boulders.boulders.ejecta.coverage = 0.2;
    boulders.boulders.ejecta.minCraterDiameter = 8.0;
    boulders.boulders.ejecta.minFreshness = 0.0;
    boulders.boulders.bedrock.exposure = terraingen::ExposureParams{};
    config.layers.push_back(boulders);
    const auto generated = GenerateTerrain(config, {}, 4);
    ASSERT_FALSE(generated.features.boulders.empty());

    terraingen::Terrain terrain = terraingen::BuildTerrain(config);
    ASSERT_EQ(terrain.layers.size(), 3u);
    terraingen::BoulderLayerOptions options = boulders.boulders;
    terrain.layers[2] = std::make_shared<terraingen::BoulderLayer>(
        options, std::dynamic_pointer_cast<const terraingen::CraterLayer>(terrain.layers[1]));
    const auto composed = terraingen::Evaluate(terrain, terrain.source->Grid());
    ExpectSameRaster(generated.heights, composed.heights);
    ASSERT_EQ(generated.features.boulders.size(), composed.features.boulders.size());
    for (std::size_t n = 0; n < composed.features.boulders.size(); ++n) {
        EXPECT_EQ(generated.features.boulders[n].id, composed.features.boulders[n].id);
    }

    boulders.boulders.ejecta.minCraterDiameter = 0.0;
    config.layers.back() = boulders;
    const auto without = GenerateTerrain(config);
    EXPECT_LT(without.features.boulders.size(), generated.features.boulders.size());
}

TEST(GenerateTerrain, ShapesMarsCraters) {
    TerrainConfig surface = Small();
    Craters(surface).morphometry = terraingen::CraterMorphometryFit::Mars;
    Craters(surface).density = terraingen::ProductionSurfaceSpec{
        terraingen::ProductionFunction::Ivanov, terraingen::ChronologyModel::Ivanov, 3.0};
    Craters(surface).ages = terraingen::UniformAgesSpec{3.0};
    Craters(surface).minDiameter = 20.0;
    EXPECT_FALSE(GenerateTerrain(surface).features.craters.Empty());
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
    const terraingen::Bounds2d bounds{1000.0, 1850.0, 1200.0, 2000.0};
    for (std::size_t n = 0; n < terrain.features.craters.Size(); ++n) {
        const auto crater = terrain.features.craters.At(n);
        EXPECT_TRUE(bounds.Expanded(0.75 * crater.diameter).Contains(crater.x, crater.y)) << n;
    }
    EXPECT_LT(terrain.heights(20, 15), WriteDem(folder, "again.tif")(20, 15) - 2.0f);
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
}

TEST(GenerateTerrain, EvaluatesTheAlbedoAsTheLibraryDoes) {
    const TerrainConfig config = terraingen::testing::WithAlbedo();
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
    EXPECT_TRUE(GenerateTerrain(Small()).albedo.empty());
}

TEST(GenerateTerrain, BuildsSphereTerrainsWithGeographicKeys) {
    TerrainConfig config = terraingen::testing::OnASphere();
    terraingen::SyntheticAlbedoSpec synthetic;
    config.albedo = terraingen::AlbedoSpec{synthetic, {terraingen::CraterAlbedoSpec{}}};
    config.filters = {terraingen::RimSmoothing{}};
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
}

TEST(GenerateTerrain, WearsCratersByDiffusionAsTheLibraryDoes) {
    TerrainConfig config = Small();
    Craters(config).degradation = terraingen::DiffusionDegradationSpec{};
    Craters(config).profile =
        terraingen::FassettThomsonSpec{terraingen::FassettThomsonRim::Plateau};
    const auto generated = GenerateTerrain(config, {}, 2);
    const terraingen::CraterModels models = BuildCraterModels(Craters(config));
    ASSERT_GT(generated.features.craters.Size(), 0u);
    const terraingen::Crater crater = generated.features.craters.At(0);
    EXPECT_DOUBLE_EQ(models.degradation->State(crater).diffusion,
                     terraingen::Fassett2022HartmannDiffusivity()(crater.diameter, crater.ageGyr) /
                         (crater.diameter * crater.diameter));
}

TEST(GenerateTerrain, AddsSecondariesAroundTheCratersBeforeThem) {
    TerrainConfig config = Small();
    Flat(config) = {400, 400, 8.0};
    CraterLayerSpec& craters = Craters(config);
    craters.generate = false;
    craters.manual = {{-3000.0, 0.0, 2000.0, 0.0, 3u}};
    terraingen::SecondaryLayerSpec secondaries;
    secondaries.secondaries.onsetCount = 40.0;
    secondaries.secondaries.minDiameter = 16.0;
    config.layers.push_back(secondaries);
    const auto generated = GenerateTerrain(config, {}, 2);
    ASSERT_GT(generated.features.craters.Size(), 2u);
    EXPECT_EQ(generated.features.craters.At(0).id, 3u);
    EXPECT_EQ(generated.features.craterAttributes.size(), generated.features.craters.Size());
}

TEST(GenerateTerrain, ShapesSecondariesWithTheirMorphometry) {
    TerrainConfig config = Small();
    Flat(config) = {400, 400, 8.0};
    CraterLayerSpec& craters = Craters(config);
    craters.generate = false;
    craters.manual = {{-3000.0, 0.0, 2000.0, 0.0, 3u}};
    terraingen::SecondaryLayerSpec secondaries;
    secondaries.secondaries.onsetCount = 40.0;
    secondaries.secondaries.minDiameter = 16.0;
    config.layers.push_back(secondaries);
    const auto lunar = GenerateTerrain(config, {}, 2);
    secondaries.morphometry = terraingen::CraterMorphometryFit::Mars;
    config.layers.back() = secondaries;
    const auto mars = GenerateTerrain(config, {}, 2);
    ASSERT_EQ(lunar.features.craters.Size(), mars.features.craters.Size());
    ASSERT_GT(mars.features.craters.Size(), 2u);
    EXPECT_FALSE(std::ranges::equal(lunar.heights.Values(), mars.heights.Values()));
}

TEST(Summary, DescribesTheTerrainAndItsTime) {
    terraingen::GeneratedTerrain terrain = GenerateTerrain(Small());
    EXPECT_GE(terrain.seconds, 0.0);
    terrain.seconds = 1.5;
    terrain.features.boulders.resize(2);
    const std::string summary = terraingen::Summary(terrain);
    EXPECT_EQ(summary, "Generated a 65x49 terrain with " +
                           std::to_string(terrain.features.craters.Size()) +
                           " craters and 2 boulders in 1.5 s");
}
