#include "configs.h"
#include "support/log_capture.h"
#include "support/raster_equality.h"
#include "support/temp_path.h"
#include "terraingen/config/generate.h"
#include "terraingen/config/json.h"
#include "terraingen/raster/resample.h"
#include "terraingen/terraingen.h"
#include "terraingen/util/files.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <vector>

using terraingen::BuildCraterModels;
using terraingen::ConfigError;
using terraingen::CraterLayerSpec;
using terraingen::CraterList;
using terraingen::GenerateTerrain;
using terraingen::TerrainConfig;
using terraingen::testing::AlbedoOf;
using terraingen::testing::BouldersOf;
using terraingen::testing::Craters;
using terraingen::testing::CratersOf;
using terraingen::testing::ExpectSameRaster;
using terraingen::testing::HeightsOf;
using terraingen::testing::Keys;
using terraingen::testing::Noise;
using terraingen::testing::RasterConfig;
using terraingen::testing::RasterSpec;
using terraingen::testing::Region;
using terraingen::testing::Small;
using terraingen::testing::TempPath;
using terraingen::testing::WriteDem;

TEST(GenerateTerrain, MatchesComposingTheLibraryCalls) {
    TerrainConfig config = Small();
    Craters(config).manual = {{4.0, -3.0, 40.0, 0.0, 0u}};
    Craters(config).apply.datum = terraingen::CraterDatum::Plane;
    Craters(config).apply.rimSmoothing.sigmaRadii = 0.05;
    const auto terrain = GenerateTerrain(config);

    auto expected = terraingen::GenerateSyntheticTerrain(65, 49, 2.0, Noise(config).Params());
    const auto models = BuildCraterModels(Craters(config));
    CraterList manual;
    manual.Append({4.0, -3.0, 40.0, 0.0, terraingen::ManualCraterId(4.0, -3.0, 40.0), true});
    const auto bounds = terraingen::config::RegionGrid(Region(config), {}).Bounds();
    const auto generate = [&](const terraingen::Bounds2d& region) {
        terraingen::CraterGenerationOptions options;
        options.dMin = 8.0;
        options.dMax = 60.0;
        options.marginRadii = terraingen::CraterReachRadii(*models.profile, Craters(config).apply);
        options.manual = manual;
        return terraingen::GenerateCraters(terraingen::PlanarCells(), region, *models.density,
                                           *models.ages, 1u, options);
    };
    const CraterList craters = generate(bounds);
    const double margin = terraingen::DatumMargin(Craters(config).apply, *models.profile, 60.0);
    const terraingen::NoiseRelief noise(Noise(config).Params());
    const terraingen::HeightSampler ground = [&](double x, double y) {
        return noise.ApplyAt(0.0f, x, y, {2.0});
    };
    terraingen::ApplyCraters(expected, generate(bounds.Expanded(margin)), models.Shape(),
                             Craters(config).apply, {2.0, 1, ground});
    EXPECT_EQ(CratersOf(terrain).craters.Ids(), craters.Ids());
    ASSERT_EQ(HeightsOf(terrain).Size(), expected.Size());
    for (std::size_t i = 0; i < expected.Size(); ++i) {
        ASSERT_EQ(HeightsOf(terrain).Values()[i], expected.Values()[i]) << i;
    }
}

TEST(GenerateTerrain, GivesTheSameTerrainOnAnyNumberOfThreads) {
    TerrainConfig config = Small();
    Craters(config).apply.rimSmoothing.sigmaRadii = 0.05;
    const auto serial = GenerateTerrain(config, {}, 1);
    const auto parallel = GenerateTerrain(config, {}, 4);
    ExpectSameRaster(HeightsOf(parallel), HeightsOf(serial));
    EXPECT_EQ(CratersOf(parallel).craters.Ids(), CratersOf(serial).craters.Ids());
}

TEST(GenerateTerrain, AppliesTheHurstExponentToTheGain) {
    TerrainConfig config = Small();
    config.layers.pop_back();
    Noise(config).stack[0].hurst = 0.6f;
    const auto terrain = GenerateTerrain(config);
    terraingen::SyntheticTerrainParams params;
    params.layers[0] = Noise(config).stack[0].EffectiveRelief();
    params.layers[0].fractal.gain = terraingen::GainFromHurst(0.6f, 2.0f);
    const auto expected = terraingen::GenerateSyntheticTerrain(65, 49, 2.0, params);
    EXPECT_TRUE(std::equal(HeightsOf(terrain).Values().begin(), HeightsOf(terrain).Values().end(),
                           expected.Values().begin()));
}

TEST(GenerateTerrain, SupportsNoLayersNoCratersAndManualCratersOnly) {
    TerrainConfig config = Small();
    config.layers.clear();
    const auto flat = GenerateTerrain(config);
    EXPECT_TRUE(std::all_of(HeightsOf(flat).Values().begin(), HeightsOf(flat).Values().end(),
                            [](float h) { return h == 0.0f; }))
        << "the datum without a base";

    config = Small();
    config.layers.pop_back();
    const auto plain = GenerateTerrain(config);
    EXPECT_TRUE(CratersOf(plain).craters.Empty());
    const auto noise = terraingen::GenerateSyntheticTerrain(65, 49, 2.0, Noise(config).Params());
    EXPECT_TRUE(std::equal(HeightsOf(plain).Values().begin(), HeightsOf(plain).Values().end(),
                           noise.Values().begin()));

    CraterLayerSpec manualOnly;
    manualOnly.generateRandom = false;
    manualOnly.manual = {{0.0, 0.0, 30.0, 0.0, 0u}, {9000.0, 0.0, 30.0, 0.0, 0u}};
    config.layers.push_back(manualOnly);
    const auto manual = GenerateTerrain(config);
    ASSERT_EQ(CratersOf(manual).craters.Size(), 1u);
    EXPECT_TRUE(CratersOf(manual).craters.At(0).manual) << "building marks manual craters";
    EXPECT_EQ(CratersOf(manual).craters.At(0).id, terraingen::ManualCraterId(0.0, 0.0, 30.0));
    TerrainConfig tiny;
    tiny.outputs = {
        terraingen::testing::Heightmap("tiny.tif", terraingen::GridRegionSpec{1, 1, 1.0})};
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
    ASSERT_GT(CratersOf(first).craters.Size(), 0u);
    ASSERT_GT(CratersOf(other).craters.Size(), 0u);
    auto ids = CratersOf(first).craters.Ids();
    const auto more = CratersOf(other).craters.Ids();
    ids.insert(ids.end(), more.begin(), more.end());
    EXPECT_EQ(CratersOf(both).craters.Ids(), ids);
}

TEST(GenerateTerrain, AddsRoughnessAsTheLibraryDoes) {
    TerrainConfig config = Small();
    const auto plain = GenerateTerrain(config);
    terraingen::RoughnessOptions roughness;
    roughness.rmsSlope = 0.2;
    roughness.minWavelength = 1.0;
    config.layers.push_back(terraingen::RoughnessLayerSpec{roughness});
    const auto rough = GenerateTerrain(config);
    const terraingen::RoughnessLayer layer(roughness);
    const auto& heights = HeightsOf(plain);
    for (std::size_t row = 0; row < heights.Height(); row += 7) {
        for (std::size_t col = 0; col < heights.Width(); col += 5) {
            const auto centre = heights.CellCenter(col, row);
            EXPECT_EQ(HeightsOf(rough)(col, row),
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
    ExpectSameRaster(
        HeightsOf(generated),
        terraingen::Evaluate(terrain, terraingen::RasterGrid::FromRaster(HeightsOf(generated))));
    const auto& heights = HeightsOf(generated);
    const auto centre = heights.Transform().WorldToPixel(10.0, -5.0);
    EXPECT_LE(heights(static_cast<std::size_t>(centre.x), static_cast<std::size_t>(centre.y)),
              0.0f);
}

TEST(GenerateTerrain, DrawsBouldersFromEachKindOfSizeDistribution) {
    TerrainConfig config = Small();
    Region(config).pixelSize = 0.5;
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
    EXPECT_FALSE(BouldersOf(generated).boulders.empty());
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
    Region(config).pixelSize = 0.5;
    terraingen::BedformOptions ridges = terraingen::MarsTransverseAeolianRidges();
    ridges.wavelength = 5.0;
    ridges.cover = 0.5;
    ridges.fieldSize = 20.0;
    config.layers.push_back(terraingen::BedformLayerSpec{ridges});
    const auto generated = GenerateTerrain(config);
    terraingen::Terrain terrain = terraingen::BuildTerrain(config);
    terrain.layers.back() = std::make_shared<terraingen::BedformLayer>(ridges);
    ExpectSameRaster(
        HeightsOf(generated),
        terraingen::Evaluate(terrain, terraingen::RasterGrid::FromRaster(HeightsOf(generated))));
}

TEST(GenerateTerrain, AddsBouldersWithEjectaFromTheLastCraterLayerBeforeThem) {
    TerrainConfig config = Small();
    Region(config).pixelSize = 0.5;
    Craters(config).minDiameter = 4.0;
    terraingen::BoulderLayerSpec boulders;
    boulders.boulders.sizes.minDiameter = 0.5;
    boulders.sizes = terraingen::GolombekRappBouldersSpec{};
    boulders.boulders.sizes.coverage = 0.03;
    boulders.boulders.ejecta.coverage = 0.2;
    boulders.boulders.ejecta.minCraterDiameter = 8.0;
    boulders.boulders.ejecta.minFreshness = 0.0;
    boulders.boulders.bedrock.exposure = terraingen::ExposureParams{};
    config.layers.push_back(boulders);
    const auto generated = GenerateTerrain(config, {}, 4);
    ASSERT_FALSE(BouldersOf(generated).boulders.empty());

    terraingen::Terrain terrain = terraingen::BuildTerrain(config);
    ASSERT_EQ(terrain.layers.size(), 3u);
    terraingen::BoulderLayerOptions options = boulders.boulders;
    terrain.layers[2] = std::make_shared<terraingen::BoulderLayer>(
        options, std::dynamic_pointer_cast<const terraingen::CraterLayer>(terrain.layers[1]));
    const auto composed =
        terraingen::Evaluate(terrain, terraingen::RasterGrid::FromRaster(HeightsOf(generated)));
    ExpectSameRaster(HeightsOf(generated), composed);
    const auto expected = terraingen::PlacedBoulders(
                              terrain, terraingen::RasterGrid::FromRaster(HeightsOf(generated)))
                              .boulders;
    const auto placed = BouldersOf(generated).boulders;
    ASSERT_EQ(placed.size(), expected.size());
    for (std::size_t n = 0; n < expected.size(); ++n) {
        EXPECT_EQ(placed[n].id, expected[n].id);
    }

    boulders.boulders.ejecta.minCraterDiameter = 0.0;
    config.layers.back() = boulders;
    const auto without = GenerateTerrain(config);
    EXPECT_LT(BouldersOf(without).boulders.size(), BouldersOf(generated).boulders.size());
}

TEST(GenerateTerrain, ShapesMarsCraters) {
    TerrainConfig surface = Small();
    Craters(surface).morphometry = terraingen::CraterMorphometryFit::Mars;
    Craters(surface).density = terraingen::ProductionSurfaceSpec{
        terraingen::ProductionFunction::Ivanov, terraingen::ChronologyModel::Ivanov, 3.0};
    Craters(surface).ages = terraingen::UniformAgesSpec{3.0};
    Craters(surface).minDiameter = 20.0;
    EXPECT_FALSE(CratersOf(GenerateTerrain(surface)).craters.Empty());
}

TEST(GenerateTerrain, LoadsResamplesAndDetailsARasterSource) {
    const auto folder = TempPath("terraingen_raster_generate");
    const auto dem = WriteDem(folder, "dem.tif");
    TerrainConfig config = RasterConfig("dem.tif");
    const auto plain = GenerateTerrain(config, folder);
    ExpectSameRaster(HeightsOf(plain), dem);
    EXPECT_EQ(plain.geoKeys, Keys());
    EXPECT_EQ(HeightsOf(plain).Transform(), dem.Transform());

    RasterSpec(config).read.scale = 1000.0;
    RasterSpec(config).read.offset = -5.0;
    const auto scaled = GenerateTerrain(config, folder);
    for (std::size_t i = 0; i < dem.Size(); ++i) {
        ASSERT_EQ(HeightsOf(scaled).Values()[i],
                  static_cast<float>(static_cast<double>(dem.Values()[i]) * 1000.0 - 5.0));
    }
    RasterSpec(config).read.scale.reset();
    RasterSpec(config).read.offset.reset();

    RasterSpec(config).read.pixelSize = 2.5;
    RasterSpec(config).read.resampling = terraingen::ResampleMethod::Bilinear;
    const auto resampled = GenerateTerrain(config, folder);
    ExpectSameRaster(HeightsOf(resampled),
                     terraingen::Resample(dem, 2.5, terraingen::ResampleMethod::Bilinear));

    RasterSpec(config).read.pixelSize.reset();
    terraingen::NoiseLayerConfig detail;
    detail.layer.fractal.wavelength = 20.0f;
    detail.amplitude = 1.5f;
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
            ASSERT_EQ(HeightsOf(detailed)(col, row), dem(col, row) + noise) << col << " " << row;
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
    craters.ages = terraingen::UniformAgesSpec{};
    craters.degradation = terraingen::FreshnessDegradationSpec{};
    craters.apply.irregularity.enabled = false;
    craters.maxDiameter = 60.0;
    craters.manual = {{1100.0, 1925.0, 50.0, 0.0, 0u, true}};
    config.layers = {craters};
    const auto terrain = GenerateTerrain(config, folder);
    const CraterList placed = CratersOf(terrain).craters;
    ASSERT_GT(placed.Size(), 2u);
    const terraingen::Bounds2d bounds{1000.0, 1850.0, 1200.0, 2000.0};
    const double reach =
        terraingen::CraterReachRadii(*BuildCraterModels(craters).profile, craters.apply);
    for (std::size_t n = 0; n < placed.Size(); ++n) {
        const auto crater = placed.At(n);
        EXPECT_TRUE(bounds.Expanded(0.5 * reach * crater.diameter).Contains(crater.x, crater.y))
            << n;
    }
    EXPECT_LT(HeightsOf(terrain)(20, 15), WriteDem(folder, "again.tif")(20, 15) - 2.0f);
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
    const auto raster = [](const std::string& path) { return terraingen::RasterSourceSpec{path}; };
    TerrainConfig config;
    config.base = terraingen::ChainSourceSpec{{raster("holes.tif"), raster("dem.tif")}};
    config.outputs = {terraingen::testing::Heightmap("filled.tif", std::nullopt)};
    const auto filled = GenerateTerrain(config, folder);
    ExpectSameRaster(HeightsOf(filled), dem);
    EXPECT_EQ(filled.geoKeys, Keys());
}

TEST(GenerateTerrain, EvaluatesTheAlbedoAsTheLibraryDoes) {
    const TerrainConfig config = terraingen::testing::WithAlbedo();
    const auto generated = GenerateTerrain(config, {}, 3);
    ASSERT_EQ(AlbedoOf(generated).size(), 3u);

    const terraingen::Terrain terrain = terraingen::BuildTerrain(config);
    const auto craters =
        std::dynamic_pointer_cast<const terraingen::CraterLayer>(terrain.layers[1]);
    ASSERT_TRUE(craters);
    const auto& synthetic = std::get<terraingen::SyntheticAlbedoSpec>(config.albedo->source);
    const terraingen::Albedo albedo{
        std::make_shared<terraingen::SyntheticAlbedo>(synthetic.Params(), terraingen::Vec2d{}),
        {std::make_shared<terraingen::CraterAlbedo>(craters)}};
    const auto grid = terraingen::RasterGrid::FromRaster(HeightsOf(generated));
    const auto expected = terraingen::EvaluateAlbedo(albedo, grid, {grid.PixelSize()});
    for (std::size_t band = 0; band < 3; ++band) {
        ExpectSameRaster(AlbedoOf(generated)[band], expected[band]);
    }
    const terraingen::Albedo plain{albedo.source, {}};
    const auto background = terraingen::EvaluateAlbedo(plain, grid, {grid.PixelSize()});
    const auto centre = AlbedoOf(generated)[0].Transform().WorldToPixel(10.0, -8.0);
    const auto col = static_cast<std::size_t>(centre.x);
    const auto row = static_cast<std::size_t>(centre.y);
    EXPECT_GT(AlbedoOf(generated)[0](col, row), background[0](col, row));
    EXPECT_TRUE(GenerateTerrain(Small()).grids.at(0).albedo.empty());
}

TEST(GenerateTerrain, BuildsSphereTerrainsWithGeographicKeys) {
    TerrainConfig config = terraingen::testing::OnASphere();
    terraingen::SyntheticAlbedoSpec synthetic;
    config.albedo = terraingen::AlbedoSpec{synthetic, {terraingen::CraterAlbedoSpec{}}};
    terraingen::AlbedoOutput albedo;
    albedo.path = "albedo.tif";
    albedo.region = Region(config);
    config.outputs.push_back(albedo);
    const auto generated = GenerateTerrain(config, {}, 2);
    const auto grid = terraingen::RasterGrid::FromRaster(HeightsOf(generated));
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    EXPECT_NEAR(grid.Spacing(moon), 4.0, 1e-9);
    EXPECT_NEAR(grid.Bounds().minX + grid.Bounds().maxX, 60.0, 1e-9);
    const CraterList placed = CratersOf(generated).craters;
    ASSERT_GT(placed.Size(), 0u);
    for (std::size_t n = 0; n < placed.Size(); ++n) {
        EXPECT_NEAR(placed.X()[n], 30.0, 0.1);
        EXPECT_NEAR(placed.Y()[n], -45.0, 0.1);
    }
    EXPECT_EQ(generated.geoKeys, terraingen::GeographicSphereKeys(1737400.0));
    EXPECT_EQ(AlbedoOf(generated).size(), 1u);

    const terraingen::Terrain terrain = terraingen::BuildTerrain(config);
    EXPECT_EQ(terrain.surface, moon);
    const auto heights = terraingen::Evaluate(terrain, grid);
    const auto centre = heights.CellCenter(20, 30);
    EXPECT_EQ(terraingen::EvaluateAt(terrain, centre.x, centre.y, 4.0), heights(20, 30));
}

TEST(GenerateTerrain, WearsCratersByDiffusionAsTheLibraryDoes) {
    TerrainConfig config = Small();
    Craters(config).degradation = terraingen::DiffusionDegradationSpec{};
    Craters(config).profile =
        terraingen::FassettThomsonSpec{terraingen::FassettThomsonRim::Plateau};
    const auto generated = GenerateTerrain(config, {}, 2);
    const terraingen::CraterModels models = BuildCraterModels(Craters(config));
    ASSERT_GT(CratersOf(generated).craters.Size(), 0u);
    const terraingen::Crater crater = CratersOf(generated).craters.At(0);
    const double fit = terraingen::Fassett2022TraskDiffusivity()(crater.diameter, crater.ageGyr) /
                       (crater.diameter * crater.diameter);
    EXPECT_EQ(CratersOf(generated).attributes[0].depthRatio,
              models.degradation->State(crater).current.depthRatio);
    EXPECT_NEAR(models.degradation->State(crater).diffusion, fit, 0.05 * fit)
        << "matched to their own profile, the ages wear craters as Fassett et al.'s fit";
}

TEST(GenerateTerrain, AddsSecondariesAroundTheCratersBeforeThem) {
    TerrainConfig config = Small();
    Region(config) = {400, 400, 8.0};
    CraterLayerSpec& craters = Craters(config);
    craters.generateRandom = false;
    craters.manual = {{-3000.0, 0.0, 2000.0, 0.0, 3u}};
    terraingen::SecondaryLayerSpec secondaries;
    secondaries.secondaries.onsetCount = 40.0;
    secondaries.secondaries.minDiameter = 16.0;
    config.layers.push_back(secondaries);
    const auto generated = GenerateTerrain(config, {}, 2);
    ASSERT_GT(CratersOf(generated).craters.Size(), 2u);
    EXPECT_EQ(CratersOf(generated).craters.At(0).id, 3u);
    EXPECT_EQ(CratersOf(generated).attributes.size(), CratersOf(generated).craters.Size());
}

TEST(GenerateTerrain, ShapesSecondariesWithTheirMorphometry) {
    TerrainConfig config = Small();
    Region(config) = {400, 400, 8.0};
    CraterLayerSpec& craters = Craters(config);
    craters.generateRandom = false;
    craters.manual = {{-3000.0, 0.0, 2000.0, 0.0, 3u}};
    terraingen::SecondaryLayerSpec secondaries;
    secondaries.secondaries.onsetCount = 40.0;
    secondaries.secondaries.minDiameter = 16.0;
    config.layers.push_back(secondaries);
    const auto lunar = GenerateTerrain(config, {}, 2);
    secondaries.morphometry = terraingen::CraterMorphometryFit::Mars;
    config.layers.back() = secondaries;
    const auto mars = GenerateTerrain(config, {}, 2);
    ASSERT_EQ(CratersOf(lunar).craters.Size(), CratersOf(mars).craters.Size());
    ASSERT_GT(CratersOf(mars).craters.Size(), 2u);
    const auto lunarHeights = HeightsOf(lunar).Values();
    const auto marsHeights = HeightsOf(mars).Values();
    EXPECT_FALSE(std::equal(lunarHeights.begin(), lunarHeights.end(), marsHeights.begin(),
                            marsHeights.end()));
}

TEST(Summary, DescribesTheTerrainItsTimeAndBackend) {
    terraingen::GeneratedTerrain terrain =
        terraingen::GenerateTerrain(Small(), {}, 0, terraingen::Backend::Cpu);
    EXPECT_GE(terrain.seconds, 0.0);
    EXPECT_EQ(terrain.backend, terraingen::Backend::Cpu);
    terrain.seconds = 1.5;
    EXPECT_EQ(terraingen::Summary(terrain), "Generated a 65x49 terrain in 1.5 s on cpu");
    terrain.backend = terraingen::Backend::Vulkan;
    EXPECT_EQ(terraingen::Summary(terrain),
              "Generated a 65x49 terrain in 1.5 s on " +
                  terraingen::DescribeBackend(terraingen::Backend::Vulkan));
}

TEST(GenerateTerrain, SplitsADemIntoItsLowPassAndTheDetailAddedBack) {
    const auto folder = TempPath("terraingen_dem_detail");
    const auto dem = WriteDem(folder, "dem.tif");
    TerrainConfig config = RasterConfig("dem.tif");
    terraingen::testing::RasterSpec(config).read.lowPass = 15.0;
    const auto smooth = GenerateTerrain(config, folder);
    const auto smoothHeights = HeightsOf(smooth).Values();
    const auto demHeights = dem.Values();
    EXPECT_FALSE(std::equal(smoothHeights.begin(), smoothHeights.end(), demHeights.begin(),
                            demHeights.end()));
    terraingen::DemDetailLayerSpec detail;
    detail.path = "dem.tif";
    detail.read.lowPass = 15.0;
    config.layers = {detail};
    const auto restored = GenerateTerrain(config, folder);
    for (std::size_t i = 0; i < dem.Size(); ++i) {
        ASSERT_NEAR(HeightsOf(restored).Values()[i], dem.Values()[i], 1e-3) << i;
    }
    detail.read.lowPass = 0.0;
    config.layers = {detail};
    EXPECT_THROW(terraingen::ValidateConfig(config, folder), ConfigError);
}

TEST(GenerateTerrain, EvaluatesEachDistinctOutputGridOnce) {
    TerrainConfig config = Small();
    const terraingen::GridRegionSpec coarse{20, 10, 8.0, 40.0, -16.0};
    config.outputs.push_back(terraingen::testing::Heightmap("again.tif"));
    config.outputs.push_back(terraingen::TerrainMeshOutput{"coarse.glb", coarse});
    config.outputs.push_back(terraingen::CraterListOutput{{"c.csv", {}, coarse}});
    const auto generated = GenerateTerrain(config);
    ASSERT_EQ(generated.grids.size(), 2u) << "lists evaluate no heights";
    const auto fine = terraingen::config::RegionGrid(Region(config), {});
    const auto wide = terraingen::config::RegionGrid(coarse, {});
    EXPECT_EQ(generated.Find(fine), &generated.grids[0]);
    EXPECT_EQ(generated.Find(wide), &generated.grids[1]);
    EXPECT_EQ(generated.Find({3, 3, {}}), nullptr);
    ExpectSameRaster(generated.grids[1].heights, terraingen::Evaluate(generated.terrain, wide));
    EXPECT_EQ(terraingen::Summary(generated).rfind("Generated 2 grids (65x49, 20x10) in ", 0), 0u)
        << terraingen::Summary(generated);

    const terraingen::testing::LogCapture capture;
    GenerateTerrain(config);
    std::vector<std::string> lines;
    for (const auto& record : capture.Matching("pipeline", terraingen::LogLevel::Info)) {
        lines.push_back(record.message);
    }
    const auto count = [&](const std::string& prefix) {
        return std::count_if(lines.begin(), lines.end(),
                             [&](const std::string& line) { return line.rfind(prefix, 0) == 0; });
    };
    EXPECT_EQ(count("Grid 1 of 2: 65x49 at "), 1);
    EXPECT_EQ(count("Grid 2 of 2: 20x10 at 8 m"), 1);
    const std::string first = "  layer 1 " + terraingen::LayerType(config.layers[0]) + " ";
    EXPECT_EQ(count(first + "in "), 2) << "once per grid";
    EXPECT_EQ(count("Time per step over 2 grids, of "), 1);
    EXPECT_EQ(count(first + " "), 1) << "and once in the totals";

    config.outputs = {};
    const auto none = GenerateTerrain(config);
    EXPECT_TRUE(none.grids.empty());
    EXPECT_EQ(terraingen::Summary(none),
              "Built the terrain; no output grids to evaluate before writing");
}

TEST(OutputGrid, CoversARegionTheBaseGridOrTheWholeBody) {
    using terraingen::config::OutputGrid;
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    const terraingen::RasterGrid base{4, 4, terraingen::GeoTransform::NorthUp(0.0, 4.0, 1.0)};
    const terraingen::GridRegionSpec region{10, 6, 3.0, 1.0, 2.0};
    const terraingen::OutputSpec heightmap = terraingen::testing::Heightmap("h.tif", region);
    EXPECT_EQ(OutputGrid(heightmap, base, {}), terraingen::config::RegionGrid(region, {}));
    EXPECT_EQ(OutputGrid(terraingen::testing::Heightmap("h.tif", std::nullopt), base, {}), base);
    EXPECT_FALSE(OutputGrid(terraingen::testing::Heightmap("h.tif", std::nullopt), {}, moon));
    EXPECT_FALSE(OutputGrid(terraingen::MeshOutput{"m.glb"}, base, moon));

    const terraingen::OutputSpec whole = terraingen::CraterListOutput{{"c.csv", {}, {}, 5000.0}};
    EXPECT_FALSE(OutputGrid(whole, {}, {})) << "a plane has no whole body";
    EXPECT_EQ(OutputGrid(whole, base, moon), base);
    const terraingen::RasterGrid body = *OutputGrid(whole, {}, moon);
    EXPECT_EQ(body, terraingen::config::WholeBodyGrid(moon, 5000.0));
    EXPECT_EQ(body.width, 2 * body.height);
    EXPECT_EQ(body.Bounds().minX, -180.0);
    EXPECT_EQ(body.Bounds().maxY, 90.0);
    EXPECT_NEAR(body.Bounds().minY, -90.0, 1e-9);
    EXPECT_LE(body.Spacing(moon), 5000.0);
    EXPECT_GT(body.Spacing(moon), 4990.0);
    EXPECT_EQ(terraingen::config::WholeBodyGrid(moon, 1e12).height, 1u);
}

TEST(GenerateTerrain, ListsCratersOverTheWholeBody) {
    TerrainConfig config;
    config.body = terraingen::SphereSurfaceSpec{50000.0};
    CraterLayerSpec craters;
    craters.minDiameter = 2000.0;
    craters.maxDiameter = 20000.0;
    craters.density = terraingen::testing::LunarDensitySpec();
    config.layers = {craters};
    config.outputs = {terraingen::CraterListOutput{{"c.csv", {}, {}, 500.0}}};
    const auto generated = GenerateTerrain(config);
    const auto grid =
        terraingen::config::WholeBodyGrid(terraingen::Surface::Sphere(50000.0), 500.0);
    const CraterList all = terraingen::PlacedCraters(generated.terrain, grid).craters;
    ASSERT_GT(all.Size(), 10u);
    double south = 90.0;
    double north = -90.0;
    for (std::size_t n = 0; n < all.Size(); ++n) {
        south = std::min(south, all.Y()[n]);
        north = std::max(north, all.Y()[n]);
    }
    EXPECT_LT(south, -45.0);
    EXPECT_GT(north, 45.0);
}

TEST(GenerateTerrain, ExpandsADemByMidpointDisplacementKeepingItsCells) {
    const auto folder = TempPath("terraingen_midpoint");
    const auto dem = WriteDem(folder, "dem.tif");
    TerrainConfig config = RasterConfig("dem.tif");
    config.layers = {terraingen::MidpointDisplacementLayerSpec{}};
    const terraingen::GridRegionSpec region{21, 21, 2.5, 1102.5, 1922.5};
    config.outputs = {terraingen::testing::Heightmap("fine.tif", region),
                      terraingen::testing::Heightmap("base.tif", std::nullopt)};
    terraingen::ValidateConfig(config, folder);
    const auto generated = GenerateTerrain(config, folder);
    ASSERT_EQ(generated.grids.size(), 2u);
    const terraingen::Raster<float>& fine = generated.grids[0].heights;
    bool expanded = false;
    for (std::size_t row = 0; row < fine.Height(); ++row) {
        for (std::size_t col = 0; col < fine.Width(); ++col) {
            const float source = dem(15 + col / 2, 10 + row / 2);
            if (col % 2 == 0 && row % 2 == 0) {
                ASSERT_EQ(fine(col, row), source) << col << " " << row;
            } else {
                expanded = expanded || fine(col, row) != source;
            }
        }
    }
    EXPECT_TRUE(expanded);
    ExpectSameRaster(generated.grids[1].heights, dem);

    auto& layer = std::get<terraingen::MidpointDisplacementLayerSpec>(config.layers[0]);
    layer.fractalDimension = 2.4;
    layer.scale = 0.0;
    layer.base = terraingen::MidpointBase::Source;
    const auto smooth = GenerateTerrain(config, folder);
    const auto cubic = GenerateTerrain(RasterConfig("dem.tif"), folder);
    const auto plain = terraingen::Evaluate(
        cubic.terrain, terraingen::RasterGrid::FromRaster(smooth.grids[0].heights));
    ExpectSameRaster(smooth.grids[0].heights, plain);
}

TEST(GenerateTerrain, ContinuesADemsSpectrumOnlyBelowItsCells) {
    const auto folder = TempPath("terraingen_spectral");
    const auto dem = WriteDem(folder, "dem.tif");
    TerrainConfig config = RasterConfig("dem.tif");
    terraingen::SpectralContinuationLayerSpec layer;
    layer.fit.maxPixels = 16.0;
    layer.minWavelength = 1.0;
    config.layers = {layer};
    const terraingen::GridRegionSpec region{21, 21, 1.25, 1102.5, 1922.5};
    config.outputs = {terraingen::testing::Heightmap("fine.tif", region),
                      terraingen::testing::Heightmap("base.tif", std::nullopt)};
    terraingen::ValidateConfig(config, folder);
    const auto generated = GenerateTerrain(config, folder);
    ASSERT_EQ(generated.grids.size(), 2u);
    ExpectSameRaster(generated.grids[1].heights, dem);
    const auto plain =
        terraingen::Evaluate(GenerateTerrain(RasterConfig("dem.tif"), folder).terrain,
                             terraingen::RasterGrid::FromRaster(generated.grids[0].heights));
    double squares = 0.0;
    for (std::size_t i = 0; i < plain.Size(); ++i) {
        const double difference = static_cast<double>(generated.grids[0].heights.Values()[i]) -
                                  static_cast<double>(plain.Values()[i]);
        squares += difference * difference;
    }
    EXPECT_GT(squares, 0.0) << "detail below the DEM's 5 m cells";

    layer.slope = 2.5;
    layer.octaveRms = 0.0;
    config.layers = {layer};
    ExpectSameRaster(GenerateTerrain(config, folder).grids[0].heights, plain);
}

namespace {

/// A DEM refined in a region of two levels about its centre, expanded by midpoint displacement,
/// with heightmaps of every level and a merged nested mesh.
TerrainConfig DemRegions(std::size_t blendCells) {
    TerrainConfig config = RasterConfig("dem.tif");
    terraingen::RegionSpec site;
    site.name = "site";
    site.centreX = 1100.0;
    site.centreY = 1925.0;
    site.repeat = terraingen::RepeatedLevelsSpec{2, 2, 2.0, 50.0};
    site.upscaling = {terraingen::MidpointDisplacementLayerSpec{}};
    site.blendCells = blendCells;
    config.regions = {site};
    terraingen::HeightmapOutput levels;
    levels.path = "site_{level}.tif";
    levels.nested = terraingen::NestedLevelRef{"site", std::nullopt};
    config.outputs = {levels, terraingen::NestedMeshOutput{"site.glb", {"site"}, 1.0}};
    return config;
}

/// The path of the `ConfigError` validating `config` throws, or "none".
std::string ErrorPath(const TerrainConfig& config, const std::filesystem::path& folder) {
    try {
        terraingen::ValidateConfig(config, folder);
    } catch (const ConfigError& error) {
        return error.Path();
    }
    return "none";
}

}  // namespace

TEST(GenerateTerrain, NestsRegionLevelsOnEachOthersCells) {
    const auto folder = TempPath("terraingen_regions");
    const auto dem = WriteDem(folder, "dem.tif");
    const TerrainConfig config = DemRegions(0);
    const auto summary = terraingen::ValidateConfig(config, folder);
    ASSERT_EQ(summary.outputGrids.size(), 4u);
    EXPECT_FALSE(summary.outputGrids[3].has_value()) << "the nested mesh";
    const auto generated = GenerateTerrain(config, folder);
    ASSERT_EQ(generated.grids.size(), 3u);
    ASSERT_EQ(generated.nested.size(), 1u);
    EXPECT_FALSE(generated.grids[0].region.has_value());
    EXPECT_EQ(generated.grids[2].region, 0u);
    ExpectSameRaster(generated.grids[0].heights, dem);
    const auto& coarse = generated.grids[1].heights;
    const auto& fine = generated.grids[2].heights;
    ASSERT_EQ(coarse.Width(), 21u);
    ASSERT_EQ(fine.Width(), 21u);
    EXPECT_EQ(coarse.CellCenter(10, 10).x, 1102.5) << "snapped to a DEM cell centre";
    EXPECT_EQ(generated.Find(terraingen::RasterGrid::FromRaster(coarse), 0u), &generated.grids[1]);
    EXPECT_EQ(generated.Find(terraingen::RasterGrid::FromRaster(coarse)), nullptr)
        << "the terrain itself was not evaluated there";
    bool expanded = false;
    for (std::size_t row = 0; row < 21; ++row) {
        for (std::size_t col = 0; col < 21; ++col) {
            if (col % 2 == 0 && row % 2 == 0) {
                ASSERT_EQ(coarse(col, row), dem(15 + col / 2, 10 + row / 2));
                ASSERT_EQ(fine(col, row), coarse(5 + col / 2, 5 + row / 2));
            } else {
                expanded = expanded || fine(col, row) != fine(col - col % 2, row - row % 2);
            }
        }
    }
    EXPECT_TRUE(expanded);

    const auto blended = GenerateTerrain(DemRegions(4), folder);
    const auto& edged = blended.grids[1].heights;
    for (std::size_t col = 1; col < 21; col += 2) {
        const double between = 0.5 * (static_cast<double>(dem(15 + col / 2, 10)) +
                                      static_cast<double>(dem(16 + col / 2, 10)));
        EXPECT_NEAR(edged(col, 0), between, 1e-4) << "linear between the cells above";
    }
    for (std::size_t row = 4; row < 17; ++row) {
        for (std::size_t col = 4; col < 17; ++col) {
            ASSERT_EQ(edged(col, row), coarse(col, row)) << "untouched inside the band";
        }
    }
}

TEST(WriteOutputs, WritesEveryLevelNestedMeshesAndAManifest) {
    const auto folder = TempPath("terraingen_region_files");
    WriteDem(folder, "dem.tif");
    TerrainConfig config = DemRegions(4);
    config.outputs.push_back(terraingen::NestedMeshOutput{"split/{level}.obj", {"site"}, 0.0});
    terraingen::TerrainMeshOutput base;
    base.path = "base.stl";
    base.nested = terraingen::NestedLevelRef{"site", 0};
    terraingen::CraterListOutput craters;
    craters.path = "site_{level}_craters.csv";
    craters.nested = terraingen::NestedLevelRef{"site", std::nullopt};
    config.outputs.push_back(base);
    config.outputs.push_back(craters);
    config.outputs.push_back(terraingen::ManifestOutput{"manifest.json", {}});
    const auto generated = GenerateTerrain(config, folder);
    const auto written = terraingen::WriteOutputs(config, generated, folder);
    ASSERT_EQ(written.size(), 3u + 1u + 3u + 1u + 3u + 1u);
    for (const char* name : {"site_0.tif", "site_2.tif", "site.glb", "split/0.obj", "split/2.obj",
                             "base.stl", "site_1_craters.csv", "manifest.json"}) {
        EXPECT_TRUE(std::filesystem::exists(folder / name)) << name;
    }
    const auto manifest = nlohmann::json::parse(terraingen::ReadFile(folder / "manifest.json"));
    ASSERT_EQ(manifest["grids"].size(), 3u);
    const auto& top = manifest["grids"][0];
    EXPECT_TRUE(top["region"].is_null());
    EXPECT_EQ(top["files"]["heightmap"], nlohmann::json::array({"site_0.tif"}));
    EXPECT_EQ(top["files"]["terrain_mesh"], nlohmann::json::array({"base.stl"}));
    EXPECT_EQ(top["files"]["nested_mesh"], nlohmann::json::array({"site.glb", "split/0.obj"}));
    const auto& finest = manifest["grids"][2];
    EXPECT_EQ(finest["region"], "site");
    EXPECT_EQ(finest["level"], 2);
    EXPECT_EQ(finest["parent"], 1);
    EXPECT_EQ(finest["pixel_size"], 1.25);
    EXPECT_EQ(finest["files"]["crater_list"], nlohmann::json::array({"site_2_craters.csv"}));
}

TEST(ValidateConfig, NamesTheRegionOrOutputAtFault) {
    const auto folder = TempPath("terraingen_region_errors");
    WriteDem(folder, "dem.tif");
    EXPECT_EQ(ErrorPath(DemRegions(4), folder), "none");
    const auto with = [&](auto change) {
        TerrainConfig config = DemRegions(4);
        change(config);
        return ErrorPath(config, folder);
    };
    EXPECT_EQ(with([](TerrainConfig& c) {
                  c.regions.push_back(c.regions[0]);
                  c.regions[1].name = "near";
                  c.regions[1].centreX = 1110.0;
              }),
              "regions[1]");
    EXPECT_EQ(with([](TerrainConfig& c) { c.regions[0].levels = {{2.5, 50.0}}; }), "regions[0]")
        << "levels and repeat";
    EXPECT_EQ(with([](TerrainConfig& c) {
                  c.base.reset();
                  c.regions[0].upscaling.clear();
                  c.outputs.pop_back();
              }),
              "regions[0].base");
    EXPECT_EQ(with([](TerrainConfig& c) {
                  terraingen::MidpointDisplacementLayerSpec bad;
                  bad.fractalDimension = 3.5;
                  c.regions[0].upscaling = {bad};
              }),
              "regions[0].upscaling[0].fractal_dimension");
    EXPECT_EQ(
        with([](TerrainConfig& c) { c.regions[0].upscaling = {terraingen::SecondaryLayerSpec{}}; }),
        "regions[0].upscaling[0]");
    const auto heightmap = [](TerrainConfig& c) -> terraingen::HeightmapOutput& {
        return std::get<terraingen::HeightmapOutput>(c.outputs[0]);
    };
    EXPECT_EQ(with([&](TerrainConfig& c) { heightmap(c).nested->region = "other"; }),
              "outputs[0].nested.region");
    EXPECT_EQ(with([&](TerrainConfig& c) { heightmap(c).nested->level = 3; }),
              "outputs[0].nested.level");
    EXPECT_EQ(with([&](TerrainConfig& c) { heightmap(c).path = "site.tif"; }), "outputs[0].path");
    EXPECT_EQ(
        with([&](TerrainConfig& c) { heightmap(c).region = terraingen::testing::SmallRegion(); }),
        "outputs[0].region");
    EXPECT_EQ(with([](TerrainConfig& c) {
                  std::get<terraingen::NestedMeshOutput>(c.outputs[1]).regions.clear();
              }),
              "outputs[1].regions");
    EXPECT_EQ(with([](TerrainConfig& c) {
                  std::get<terraingen::NestedMeshOutput>(c.outputs[1]).path = "site.tif";
              }),
              "outputs[1].path");
    EXPECT_EQ(with([](TerrainConfig& c) {
                  terraingen::RegionSpec apart = c.regions[0];
                  apart.name = "apart";
                  apart.base = terraingen::GridRegionSpec{65, 65, 2.0, 0.0, 0.0};
                  apart.centreX = 0.0;
                  apart.centreY = 0.0;
                  apart.upscaling.clear();
                  c.regions.push_back(apart);
                  std::get<terraingen::NestedMeshOutput>(c.outputs[1]).regions.push_back("apart");
              }),
              "outputs[1].regions[1]")
        << "on another base";
    EXPECT_EQ(with([](TerrainConfig& c) {
                  c.regions.push_back(c.regions[0]);
                  c.regions[1].name = "far";
                  c.regions[1].centreX = 1030.0;
                  c.regions[1].centreY = 1960.0;
                  c.regions[1].repeat->size = 20.0;
                  auto& mesh = std::get<terraingen::NestedMeshOutput>(c.outputs[1]);
                  mesh.regions.push_back("far");
                  mesh.path = "{level}.glb";
              }),
              "outputs[1].path")
        << "{level} without {region} for two regions";
    EXPECT_EQ(with([](TerrainConfig& c) {
                  c.outputs.push_back(terraingen::ManifestOutput{"m.json", {"nowhere"}});
              }),
              "outputs[2].regions[0]");
}
