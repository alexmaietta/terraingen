#include "configs.h"
#include "support/log_capture.h"
#include "support/raster_equality.h"
#include "support/resample.h"
#include "support/temp_path.h"
#include "terraingen/config/generate.h"
#include "terraingen/config/json.h"
#include "terraingen/raster/resample.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using terraingen::BuildCraterModels;
using terraingen::ConfigError;
using terraingen::CraterLayerSpec;
using terraingen::CraterList;
using terraingen::CraterListOutput;
using terraingen::GenerateFromConfig;
using terraingen::ListFormat;
using terraingen::TerrainGenConfig;
using terraingen::testing::AlbedoOf;
using terraingen::testing::BouldersOf;
using terraingen::testing::Craters;
using terraingen::testing::CratersOf;
using terraingen::testing::ExpectSameRaster;
using terraingen::testing::GridRegion;
using terraingen::testing::HeightsOf;
using terraingen::testing::Keys;
using terraingen::testing::Noise;
using terraingen::testing::RasterConfig;
using terraingen::testing::RasterSpec;
using terraingen::testing::Region;
using terraingen::testing::Small;
using terraingen::testing::SmallRegion;
using terraingen::testing::TempPath;
using terraingen::testing::WriteDem;

TEST(GenerateFromConfig, MatchesComposingTheLibraryCalls) {
    TerrainGenConfig config = Small();
    Craters(config).manual = {{4.0, -3.0, 40.0, 0.0, 0u}};
    Craters(config).apply.datum = terraingen::CraterDatum::Plane;
    Craters(config).apply.rimSmoothing.sigmaRadii = 0.05;
    const auto terrain = GenerateFromConfig(config);

    auto expected = terraingen::GenerateSyntheticTerrain(65, 49, 2.0, Noise(config).Params());
    const auto models = BuildCraterModels(Craters(config));
    CraterList manual;
    manual.Append({4.0, -3.0, 40.0, 0.0, terraingen::ManualCraterId(4.0, -3.0, 40.0), true});
    const auto bounds = terraingen::testing::SmallRegion().Grid().Bounds();
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

TEST(GenerateFromConfig, GivesTheSameTerrainOnAnyNumberOfThreads) {
    TerrainGenConfig config = Small();
    Craters(config).apply.rimSmoothing.sigmaRadii = 0.05;
    const auto serial = GenerateFromConfig(config, {}, 1);
    const auto parallel = GenerateFromConfig(config, {}, 4);
    ExpectSameRaster(HeightsOf(parallel), HeightsOf(serial));
    EXPECT_EQ(CratersOf(parallel).craters.Ids(), CratersOf(serial).craters.Ids());
}

TEST(GenerateFromConfig, AppliesTheHurstExponentToTheGain) {
    TerrainGenConfig config = Small();
    config.layers.pop_back();
    Noise(config).stack[0].hurst = 0.6f;
    const auto terrain = GenerateFromConfig(config);
    terraingen::SyntheticTerrainParams params;
    params.layers[0] = Noise(config).stack[0].EffectiveRelief();
    params.layers[0].fractal.gain = terraingen::GainFromHurst(0.6f, 2.0f);
    const auto expected = terraingen::GenerateSyntheticTerrain(65, 49, 2.0, params);
    EXPECT_TRUE(std::equal(HeightsOf(terrain).Values().begin(), HeightsOf(terrain).Values().end(),
                           expected.Values().begin()));
}

TEST(GenerateFromConfig, SupportsNoLayersNoCratersAndManualCratersOnly) {
    TerrainGenConfig config = Small();
    config.layers.clear();
    const auto flat = GenerateFromConfig(config);
    EXPECT_TRUE(std::all_of(HeightsOf(flat).Values().begin(), HeightsOf(flat).Values().end(),
                            [](float h) { return h == 0.0f; }))
        << "the datum without a base";

    config = Small();
    config.layers.pop_back();
    const auto plain = GenerateFromConfig(config);
    EXPECT_TRUE(CratersOf(plain).craters.Empty());
    const auto noise = terraingen::GenerateSyntheticTerrain(65, 49, 2.0, Noise(config).Params());
    EXPECT_TRUE(std::equal(HeightsOf(plain).Values().begin(), HeightsOf(plain).Values().end(),
                           noise.Values().begin()));

    CraterLayerSpec manualOnly;
    manualOnly.generateRandom = false;
    manualOnly.manual = {{0.0, 0.0, 30.0, 0.0, 0u}, {9000.0, 0.0, 30.0, 0.0, 0u}};
    config.layers.push_back(manualOnly);
    const auto manual = GenerateFromConfig(config);
    ASSERT_EQ(CratersOf(manual).craters.Size(), 1u);
    EXPECT_TRUE(CratersOf(manual).craters.At(0).manual) << "building marks manual craters";
    EXPECT_EQ(CratersOf(manual).craters.At(0).id, terraingen::ManualCraterId(0.0, 0.0, 30.0));
    TerrainGenConfig tiny;
    tiny.outputs = {
        terraingen::testing::Heightmap("tiny.tif", terraingen::testing::GridRegion{1, 1, 1.0})};
    EXPECT_THROW(GenerateFromConfig(tiny), ConfigError);
}

TEST(GenerateFromConfig, RunsLayersInOrderAndCollectsEveryLayersCraters) {
    TerrainGenConfig config = Small();
    CraterLayerSpec second = Craters(config);
    second.seed = 2;
    config.layers.push_back(second);
    const auto both = GenerateFromConfig(config);
    config.layers.pop_back();
    const auto first = GenerateFromConfig(config);
    config.layers.pop_back();
    config.layers.push_back(second);
    const auto other = GenerateFromConfig(config);
    ASSERT_GT(CratersOf(first).craters.Size(), 0u);
    ASSERT_GT(CratersOf(other).craters.Size(), 0u);
    auto ids = CratersOf(first).craters.Ids();
    const auto more = CratersOf(other).craters.Ids();
    ids.insert(ids.end(), more.begin(), more.end());
    EXPECT_EQ(CratersOf(both).craters.Ids(), ids);
}

TEST(GenerateFromConfig, AddsRoughnessAsTheLibraryDoes) {
    TerrainGenConfig config = Small();
    const auto plain = GenerateFromConfig(config);
    terraingen::RoughnessOptions roughness;
    roughness.rmsSlope = 0.2;
    roughness.minWavelength = 1.0;
    config.layers.push_back(terraingen::RoughnessLayerSpec{roughness});
    const auto rough = GenerateFromConfig(config);
    const terraingen::RoughnessLayer layer(roughness);
    const auto& heights = HeightsOf(plain);
    for (std::size_t row = 0; row < heights.Height(); row += 7) {
        for (std::size_t col = 0; col < heights.Width(); col += 5) {
            const auto center = heights.CellCenter(col, row);
            EXPECT_EQ(HeightsOf(rough)(col, row),
                      layer.ApplyAt(heights(col, row), center.x, center.y, {2.0}));
        }
    }
}

TEST(GenerateFromConfig, AppliesTransformsAndRegionsAsTheLibraryDoes) {
    TerrainGenConfig config = Small();
    const terraingen::MaskedLayerSpec region{terraingen::CircleRegionSpec{10.0, -5.0, 20.0}, 6.0,
                                             terraingen::BlendShape::Linear,
                                             terraingen::ClampLayerSpec{std::nullopt, 0.0}};
    config.layers.push_back(terraingen::ScaleLayerSpec{2.0, 1.0});
    config.layers.push_back(region);
    const auto generated = GenerateFromConfig(config);

    terraingen::TerrainGenerator terrain = terraingen::BuildGenerator(config);
    ASSERT_EQ(terrain.layers.size(), 4u);
    terrain.layers[2] = std::make_shared<terraingen::ScaleLayer>(2.0, 1.0);
    terrain.layers[3] = std::make_shared<terraingen::MaskedLayer>(
        std::make_shared<terraingen::ClampLayer>(std::nullopt, 0.0),
        terraingen::RegionShape::Circle({10.0, -5.0}, 20.0), 6.0, terraingen::BlendShape::Linear);
    ExpectSameRaster(
        HeightsOf(generated),
        terraingen::Evaluate(terrain, terraingen::RasterGrid::FromRaster(HeightsOf(generated))));
    const auto& heights = HeightsOf(generated);
    const auto center = heights.Transform().WorldToPixel(10.0, -5.0);
    EXPECT_LE(heights(static_cast<std::size_t>(center.x), static_cast<std::size_t>(center.y)),
              0.0f);
}

TEST(GenerateFromConfig, DrawsBouldersFromEachKindOfSizeDistribution) {
    TerrainGenConfig config = Small();
    Region(config) = GridRegion{65, 49, 0.5};
    terraingen::BoulderLayerSpec boulders;
    boulders.boulders.sizes.minDiameter = 0.5;
    boulders.boulders.ejecta.minCraterDiameter = 8.0;
    boulders.boulders.ejecta.minFreshness = 0.0;
    boulders.sizes =
        terraingen::Sp8023IntercraterBouldersSpec{terraingen::SpecificationCurve::Maximum};
    boulders.ejectaSizes = terraingen::Sp8023CraterBouldersSpec{};
    boulders.extrapolate = true;
    config.layers.push_back(boulders);
    const auto generated = GenerateFromConfig(config);
    EXPECT_FALSE(BouldersOf(generated).boulders.empty());
    for (const terraingen::BoulderSizesSpec& sizes :
         {terraingen::BoulderSizesSpec{terraingen::SynthterrainBouldersSpec{}},
          terraingen::BoulderSizesSpec{terraingen::PowerLawSpec{0.001, 2.5}},
          terraingen::BoulderSizesSpec{terraingen::PiecewisePowerLawSpec{{{0.5, 0.001, 2.5}}}},
          terraingen::BoulderSizesSpec{terraingen::TabulatedSpec{{0.5, 10.0}, {0.01, 1e-5}}}}) {
        boulders.sizes = sizes;
        config.layers.back() = boulders;
        EXPECT_NO_THROW(GenerateFromConfig(config));
    }
}

TEST(GenerateFromConfig, AddsBedformsAsTheLibraryDoes) {
    TerrainGenConfig config = Small();
    Region(config) = GridRegion{65, 49, 0.5};
    terraingen::BedformOptions ridges = terraingen::MarsTransverseAeolianRidges();
    ridges.wavelength = 5.0;
    ridges.cover = 0.5;
    ridges.fieldSize = 20.0;
    config.layers.push_back(terraingen::BedformLayerSpec{ridges});
    const auto generated = GenerateFromConfig(config);
    terraingen::TerrainGenerator terrain = terraingen::BuildGenerator(config);
    terrain.layers.back() = std::make_shared<terraingen::BedformLayer>(ridges);
    ExpectSameRaster(
        HeightsOf(generated),
        terraingen::Evaluate(terrain, terraingen::RasterGrid::FromRaster(HeightsOf(generated))));
}

TEST(GenerateFromConfig, AddsBouldersWithEjectaFromTheLastCraterLayerBeforeThem) {
    TerrainGenConfig config = Small();
    Region(config) = GridRegion{65, 49, 0.5};
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
    const auto generated = GenerateFromConfig(config, {}, 4);
    ASSERT_FALSE(BouldersOf(generated).boulders.empty());

    terraingen::TerrainGenerator terrain = terraingen::BuildGenerator(config);
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
    const auto without = GenerateFromConfig(config);
    EXPECT_LT(BouldersOf(without).boulders.size(), BouldersOf(generated).boulders.size());
}

TEST(GenerateFromConfig, ShapesMarsCraters) {
    TerrainGenConfig surface = Small();
    Craters(surface).morphometry = terraingen::CraterMorphometryFit::Mars;
    Craters(surface).density = terraingen::ProductionSurfaceSpec{
        terraingen::ProductionFunction::Ivanov, terraingen::ChronologyModel::Ivanov, 3.0};
    Craters(surface).ages = terraingen::UniformAgesSpec{3.0};
    Craters(surface).minDiameter = 20.0;
    EXPECT_FALSE(CratersOf(GenerateFromConfig(surface)).craters.Empty());
}

TEST(GenerateFromConfig, LoadsResamplesAndDetailsARasterSource) {
    const auto folder = TempPath("terraingen_raster_generate");
    const auto dem = WriteDem(folder, "dem.tif");
    TerrainGenConfig config = RasterConfig("dem.tif");
    const auto plain = GenerateFromConfig(config, folder);
    ExpectSameRaster(HeightsOf(plain), dem);
    EXPECT_EQ(plain.geoKeys, Keys());
    EXPECT_EQ(HeightsOf(plain).Transform(), dem.Transform());

    RasterSpec(config).read.scale = 1000.0;
    RasterSpec(config).read.offset = -5.0;
    const auto scaled = GenerateFromConfig(config, folder);
    for (std::size_t i = 0; i < dem.Size(); ++i) {
        ASSERT_EQ(HeightsOf(scaled).Values()[i],
                  static_cast<float>(static_cast<double>(dem.Values()[i]) * 1000.0 - 5.0));
    }
    RasterSpec(config).read.scale.reset();
    RasterSpec(config).read.offset.reset();

    const std::vector<terraingen::OutputSpec> outputs = config.outputs;
    RasterSpec(config).upsampling.type = "bilinear";
    config.outputs = {
        terraingen::testing::Heightmap("fine.tif", GridRegion{80, 60, 2.5, 1100.0, 1925.0})};
    const auto resampled = GenerateFromConfig(config, folder);
    ExpectSameRaster(HeightsOf(resampled),
                     terraingen::testing::Resample(dem, 2.5, terraingen::ResampleMethod::Bilinear));

    RasterSpec(config).upsampling = {};
    config.outputs = outputs;
    terraingen::NoiseLayerConfig detail;
    detail.layer.fractal.wavelength = 20.0f;
    detail.amplitude = 1.5f;
    terraingen::NoiseLayerSpec detailLayer;
    detailLayer.stack = {detail};
    config.layers = {detailLayer};
    const auto detailed = GenerateFromConfig(config, folder);
    const terraingen::NoiseLayer layer = detail.Effective();
    for (std::size_t row = 0; row < dem.Height(); ++row) {
        for (std::size_t col = 0; col < dem.Width(); ++col) {
            const auto center = dem.CellCenter(col, row);
            const float noise =
                terraingen::EvaluateNoiseLayers(static_cast<float>(center.x - 1000.0),
                                                static_cast<float>(center.y - 2000.0), &layer, 1);
            ASSERT_EQ(HeightsOf(detailed)(col, row), dem(col, row) + noise) << col << " " << row;
        }
    }
}

TEST(GenerateFromConfig, PlacesCratersInTheRastersCoordinates) {
    const auto folder = TempPath("terraingen_raster_craters");
    WriteDem(folder, "dem.tif");
    TerrainGenConfig config = RasterConfig("dem.tif");
    CraterLayerSpec craters;
    craters.density = terraingen::PowerLawSpec{0.1, 2.0};
    craters.minDiameter = 15.0;
    craters.ages = terraingen::UniformAgesSpec{};
    craters.degradation = terraingen::FreshnessDegradationSpec{};
    craters.apply.irregularity.enabled = false;
    craters.maxDiameter = 60.0;
    craters.manual = {{1100.0, 1925.0, 50.0, 0.0, 0u, true}};
    config.layers = {craters};
    const auto terrain = GenerateFromConfig(config, folder);
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

TEST(GenerateFromConfig, FillsARastersGapsFromTheSourcesChainedAfterIt) {
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
    TerrainGenConfig config;
    config.source = terraingen::ChainSourceSpec{{raster("holes.tif"), raster("dem.tif")}};
    config.outputs = {terraingen::testing::Heightmap("filled.tif", std::nullopt)};
    const auto filled = GenerateFromConfig(config, folder);
    ExpectSameRaster(HeightsOf(filled), dem);
    EXPECT_EQ(filled.geoKeys, Keys());
}

TEST(GenerateFromConfig, EvaluatesTheAlbedoAsTheLibraryDoes) {
    const TerrainGenConfig config = terraingen::testing::WithAlbedo();
    const auto generated = GenerateFromConfig(config, {}, 3);
    ASSERT_EQ(AlbedoOf(generated).size(), 3u);

    const terraingen::TerrainGenerator terrain = terraingen::BuildGenerator(config);
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
    const auto center = AlbedoOf(generated)[0].Transform().WorldToPixel(10.0, -8.0);
    const auto col = static_cast<std::size_t>(center.x);
    const auto row = static_cast<std::size_t>(center.y);
    EXPECT_GT(AlbedoOf(generated)[0](col, row), background[0](col, row));
    EXPECT_TRUE(GenerateFromConfig(Small()).grids.at(0).albedo.empty());
}

TEST(GenerateFromConfig, BuildsSphereTerrainsWithGeographicKeys) {
    TerrainGenConfig config = terraingen::testing::OnASphere();
    terraingen::SyntheticAlbedoSpec synthetic;
    config.albedo = terraingen::AlbedoSpec{synthetic, {terraingen::CraterAlbedoSpec{}}};
    terraingen::AlbedoOutput albedo;
    albedo.path = "albedo.tif";
    albedo.area = Region(config);
    config.outputs.push_back(albedo);
    const auto generated = GenerateFromConfig(config, {}, 2);
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

    const terraingen::TerrainGenerator terrain = terraingen::BuildGenerator(config);
    EXPECT_EQ(terrain.surface, moon);
    const auto heights = terraingen::Evaluate(terrain, grid);
    const auto center = heights.CellCenter(20, 30);
    EXPECT_EQ(terraingen::HeightAt(terrain, center.x, center.y, 4.0), heights(20, 30));
}

TEST(GenerateFromConfig, WearsCratersByDiffusionAsTheLibraryDoes) {
    TerrainGenConfig config = Small();
    Craters(config).degradation = terraingen::DiffusionDegradationSpec{};
    Craters(config).profile =
        terraingen::FassettThomsonSpec{terraingen::FassettThomsonRim::Plateau};
    const auto generated = GenerateFromConfig(config, {}, 2);
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

TEST(GenerateFromConfig, AddsSecondariesAroundTheCratersBeforeThem) {
    TerrainGenConfig config = Small();
    Region(config) = GridRegion{400, 400, 8.0};
    CraterLayerSpec& craters = Craters(config);
    craters.generateRandom = false;
    craters.manual = {{-3000.0, 0.0, 2000.0, 0.0, 3u}};
    terraingen::SecondaryLayerSpec secondaries;
    secondaries.secondaries.onsetCount = 40.0;
    secondaries.secondaries.minDiameter = 16.0;
    config.layers.push_back(secondaries);
    const auto generated = GenerateFromConfig(config, {}, 2);
    ASSERT_GT(CratersOf(generated).craters.Size(), 2u);
    EXPECT_EQ(CratersOf(generated).craters.At(0).id, 3u);
    EXPECT_EQ(CratersOf(generated).attributes.size(), CratersOf(generated).craters.Size());
}

TEST(GenerateFromConfig, ShapesSecondariesWithTheirMorphometry) {
    TerrainGenConfig config = Small();
    Region(config) = GridRegion{400, 400, 8.0};
    CraterLayerSpec& craters = Craters(config);
    craters.generateRandom = false;
    craters.manual = {{-3000.0, 0.0, 2000.0, 0.0, 3u}};
    terraingen::SecondaryLayerSpec secondaries;
    secondaries.secondaries.onsetCount = 40.0;
    secondaries.secondaries.minDiameter = 16.0;
    config.layers.push_back(secondaries);
    const auto lunar = GenerateFromConfig(config, {}, 2);
    secondaries.morphometry = terraingen::CraterMorphometryFit::Mars;
    config.layers.back() = secondaries;
    const auto mars = GenerateFromConfig(config, {}, 2);
    ASSERT_EQ(CratersOf(lunar).craters.Size(), CratersOf(mars).craters.Size());
    ASSERT_GT(CratersOf(mars).craters.Size(), 2u);
    const auto lunarHeights = HeightsOf(lunar).Values();
    const auto marsHeights = HeightsOf(mars).Values();
    EXPECT_FALSE(std::equal(lunarHeights.begin(), lunarHeights.end(), marsHeights.begin(),
                            marsHeights.end()));
}

TEST(Summary, DescribesTheTerrainItsTimeAndBackend) {
    terraingen::GeneratedOutputs terrain =
        terraingen::GenerateFromConfig(Small(), {}, 0, terraingen::Backend::Cpu);
    EXPECT_GE(terrain.seconds, 0.0);
    EXPECT_EQ(terrain.backend, terraingen::Backend::Cpu);
    terrain.seconds = 1.5;
    EXPECT_EQ(terraingen::Summary(terrain), "Generated a 65x49 terrain in 1.5 s on cpu");
    terrain.backend = terraingen::Backend::Vulkan;
    EXPECT_EQ(terraingen::Summary(terrain),
              "Generated a 65x49 terrain in 1.5 s on " +
                  terraingen::DescribeBackend(terraingen::Backend::Vulkan));
}

TEST(GenerateFromConfig, GeneratesASyntheticSourceOverEachOutputsRegion) {
    TerrainGenConfig config;
    terraingen::SyntheticSourceSpec synthetic;
    synthetic.elevation = 4.0;
    terraingen::NoiseLayerConfig layer;
    layer.layer.fractal.wavelength = 120.0f;
    synthetic.stack = {layer};
    config.source = synthetic;
    const GridRegion region{33, 17, 3.0, 50.0, -20.0};
    config.outputs = {terraingen::testing::Heightmap("synthetic.tif", region)};
    const auto generated = GenerateFromConfig(config);
    const terraingen::SyntheticSource expected(terraingen::StackParams(synthetic.stack), 4.0f);
    terraingen::Raster<float> heights = region.Grid().MakeRaster();
    expected.Fill(heights);
    ExpectSameRaster(HeightsOf(generated), heights);
    config.outputs = {terraingen::testing::Heightmap("synthetic.tif", std::nullopt)};
    EXPECT_THROW(terraingen::ValidateConfig(config), ConfigError)
        << "synthetic noise has no grid of its own to cover";
}

TEST(GenerateFromConfig, EvaluatesEachDistinctOutputGridOnce) {
    TerrainGenConfig config = Small();
    const GridRegion coarse{20, 10, 8.0, 40.0, -16.0};
    config.outputs.push_back(terraingen::testing::Heightmap("again.tif"));
    config.outputs.push_back(terraingen::TerrainMeshOutput{"coarse.glb", coarse});
    config.outputs.push_back(terraingen::CraterListOutput{{"c.csv", {}, coarse}});
    const auto generated = GenerateFromConfig(config);
    ASSERT_EQ(generated.grids.size(), 2u) << "the list shares the mesh's grid";
    const auto fine = terraingen::testing::SmallRegion().Grid();
    const auto wide = coarse.Grid();
    EXPECT_EQ(generated.Find(fine), &generated.grids[0]);
    EXPECT_EQ(generated.Find(wide), &generated.grids[1]);
    EXPECT_EQ(generated.Find({3, 3, {}}), nullptr);
    EXPECT_THROW(generated.Grid(), std::runtime_error) << "two grids";
    ExpectSameRaster(generated.grids[1].heights, terraingen::Evaluate(generated.generator, wide));
    EXPECT_FALSE(generated.grids[0].craters) << "no list covers the fine grid";
    ASSERT_TRUE(generated.grids[1].craters);
    EXPECT_EQ(generated.grids[1].craters->craters.Ids(),
              terraingen::PlacedCraters(generated.generator, wide).craters.Ids());
    ASSERT_TRUE(generated.grids[1].mesh);
    EXPECT_EQ(generated.grids[1].mesh->triangles.size(), 2u * 19u * 9u);
    EXPECT_EQ(terraingen::Summary(generated).rfind("Generated 2 grids (65x49, 20x10) in ", 0), 0u)
        << terraingen::Summary(generated);

    const terraingen::testing::LogCapture capture;
    GenerateFromConfig(config);
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
    const auto none = GenerateFromConfig(config);
    EXPECT_TRUE(none.grids.empty());
    EXPECT_THROW(none.Grid(), std::runtime_error) << "no grid";
    EXPECT_EQ(terraingen::Summary(none).rfind("Generated the outputs, with no heights to evaluate, "
                                              "in ",
                                              0),
              0u)
        << terraingen::Summary(none);
}

namespace {

/// A heightmap over `extent` in meters at `pixelSize`, either unset.
terraingen::HeightmapOutput AreaHeightmap(std::optional<terraingen::ExtentSpec> extent,
                                          std::optional<double> pixelSize) {
    terraingen::HeightmapOutput output;
    output.area.extentM = std::move(extent);
    output.area.pixelSize = pixelSize;
    return output;
}

/// An extent by its corners.
terraingen::ExtentSpec Corners(double minX, double minY, double maxX, double maxY) {
    terraingen::ExtentSpec extent;
    extent.min = {minX, minY};
    extent.max = {maxX, maxY};
    return extent;
}

}  // namespace

TEST(OutputGrid, SnapsTheExtentOutwardToWholeCellsFromItsMinCorner) {
    using terraingen::GeoTransform;
    using terraingen::RasterGrid;
    using terraingen::config::OutputGrid;
    const RasterGrid exact{10, 6, GeoTransform::NorthUp(-14.0, 11.0, 3.0)};
    EXPECT_EQ(OutputGrid(AreaHeightmap(Corners(-14.0, -7.0, 16.0, 11.0), 3.0), {}, {}), exact);
    terraingen::ExtentSpec centered;
    centered.center = {1.0, 2.0};
    centered.size = {30.0, 18.0};
    EXPECT_EQ(OutputGrid(AreaHeightmap(centered, 3.0), {}, {}), exact)
        << "center and size give the same box as its corners";

    const RasterGrid grown{11, 7, GeoTransform::NorthUp(-14.0, 14.0, 3.0)};
    EXPECT_EQ(OutputGrid(AreaHeightmap(Corners(-14.0, -7.0, 16.5, 11.1), 3.0), {}, {}), grown)
        << "a partial cell is grown outward past the max corner, the min corner kept";
    EXPECT_EQ(OutputGrid(AreaHeightmap(Corners(-14.0, -7.0, 16.0 + 1e-9, 11.0), 3.0), {}, {}),
              exact)
        << "rounding below the tolerance adds no cell";
    const RasterGrid single{1, 1, GeoTransform::NorthUp(0.0, 3.0, 3.0)};
    EXPECT_EQ(OutputGrid(AreaHeightmap(Corners(0.0, 0.0, 0.5, 0.5), 3.0), {}, {}), single);
    const auto first = *OutputGrid(AreaHeightmap(Corners(-14.0, -7.0, 16.0, 11.0), 3.0), {}, {});
    EXPECT_EQ(first.transform.PixelToWorld(0.5, 0.5).x, -12.5) << "sampled at cell centers";
    EXPECT_EQ(first.transform.PixelToWorld(0.5, 5.5).y, -5.5);

    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    terraingen::HeightmapOutput body;
    body.area.pixelSize = 4.0;
    body.area.extentDeg = Corners(10.0, -20.0, 10.5, -19.5);
    const double cell = 4.0 / moon.UnitLength();
    const RasterGrid degrees = *OutputGrid(body, {}, moon);
    EXPECT_EQ(degrees.transform.PixelToWorld(0.0, 0.0).x, 10.0);
    EXPECT_NEAR(degrees.Spacing(moon), 4.0, 1e-9);
    EXPECT_EQ(degrees.width, static_cast<std::size_t>(std::ceil(0.5 / cell)));
    EXPECT_FALSE(OutputGrid(AreaHeightmap(Corners(0.0, 0.0, 1.0, 1.0), 1.0), {}, moon))
        << "extent_m is not read on a body";
}

TEST(OutputGrid, DefaultsToTheSourcesExtentAndCellSize) {
    using terraingen::GeoTransform;
    using terraingen::RasterGrid;
    using terraingen::config::OutputGrid;
    const RasterGrid base{40, 30, GeoTransform::NorthUp(1000.0, 2000.0, 5.0)};
    EXPECT_EQ(OutputGrid(AreaHeightmap(std::nullopt, std::nullopt), base, {}), base)
        << "neither gives the source's grid itself";
    EXPECT_EQ(OutputGrid(AreaHeightmap(std::nullopt, 2.0), base, {}),
              (RasterGrid{100, 75, GeoTransform::NorthUp(1000.0, 2000.0, 2.0)}))
        << "the source's extent at the output's pixel size";
    EXPECT_EQ(
        OutputGrid(AreaHeightmap(Corners(1010.0, 1900.0, 1030.0, 1912.0), std::nullopt), base, {}),
        (RasterGrid{4, 3, GeoTransform::NorthUp(1010.0, 1915.0, 5.0)}))
        << "the output's extent at the source's cell size";
    EXPECT_FALSE(OutputGrid(AreaHeightmap(std::nullopt, 2.0), {}, {}))
        << "an unbounded source has no extent";
    EXPECT_FALSE(OutputGrid(AreaHeightmap(Corners(0.0, 0.0, 4.0, 4.0), std::nullopt), {}, {}))
        << "nor a cell size";
}

TEST(OutputGrid, CoversAnAreaTheBaseGridOrTheWholeBody) {
    using terraingen::config::OutputGrid;
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    const terraingen::RasterGrid base{4, 4, terraingen::GeoTransform::NorthUp(0.0, 4.0, 1.0)};
    const GridRegion region{10, 6, 3.0, 1.0, 2.0};
    const terraingen::OutputSpec heightmap = terraingen::testing::Heightmap("h.tif", region);
    EXPECT_EQ(OutputGrid(heightmap, base, {}), region.Grid());
    EXPECT_EQ(OutputGrid(terraingen::testing::Heightmap("h.tif", std::nullopt), base, {}), base);
    EXPECT_FALSE(OutputGrid(terraingen::testing::Heightmap("h.tif", std::nullopt), {}, moon));
    EXPECT_FALSE(OutputGrid(terraingen::MeshOutput{"m.glb"}, base, moon));
    EXPECT_FALSE(OutputGrid(terraingen::ReportOutput{"r.json"}, base, moon));

    terraingen::CraterListOutput wholeList;
    wholeList.path = "c.csv";
    wholeList.area.pixelSize = 5000.0;
    const terraingen::OutputSpec whole = wholeList;
    EXPECT_FALSE(OutputGrid(whole, {}, {})) << "a plane has no whole body";
    EXPECT_EQ(OutputGrid(whole, base, moon)->Bounds().minX, base.Bounds().minX)
        << "the source's extent at the list's pixel size";
    EXPECT_NEAR(OutputGrid(whole, base, moon)->Spacing(moon), 5000.0, 1e-6);
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

TEST(GenerateFromConfig, ListsCratersOverTheWholeBody) {
    TerrainGenConfig config;
    config.body = terraingen::SphereSurfaceSpec{50000.0};
    CraterLayerSpec craters;
    craters.minDiameter = 2000.0;
    craters.maxDiameter = 20000.0;
    craters.density = terraingen::testing::LunarDensitySpec();
    config.layers = {craters};
    terraingen::CraterListOutput list;
    list.path = "c.csv";
    list.area.pixelSize = 500.0;
    config.outputs = {list};
    const auto generated = GenerateFromConfig(config);
    const auto grid =
        terraingen::config::WholeBodyGrid(terraingen::Surface::Sphere(50000.0), 500.0);
    ASSERT_TRUE(generated.Grid().craters);
    EXPECT_EQ(generated.Grid().grid, grid);
    EXPECT_TRUE(generated.Grid().heights.Empty()) << "a list evaluates no heights";
    const CraterList all = generated.Grid().craters->craters;
    EXPECT_EQ(all.Ids(), terraingen::PlacedCraters(generated.generator, grid, 500.0).craters.Ids());
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

TEST(GenerateFromConfig, ExpandsADemByDiamondSquareKeepingItsCells) {
    const auto folder = TempPath("terraingen_midpoint");
    const auto dem = WriteDem(folder, "dem.tif");
    TerrainGenConfig config = RasterConfig("dem.tif");
    RasterSpec(config).upsampling.type = "diamond_square";
    const GridRegion region{21, 21, 2.5, 1102.5, 1922.5};
    config.outputs = {terraingen::testing::Heightmap("fine.tif", region),
                      terraingen::testing::Heightmap("base.tif", std::nullopt)};
    terraingen::ValidateConfig(config, folder);
    const auto generated = GenerateFromConfig(config, folder);
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

    terraingen::DiamondSquareOptions& fractal = RasterSpec(config).upsampling.diamondSquare;
    fractal.fractalDimension = 2.4;
    fractal.scale = 0.0;
    fractal.interp = terraingen::DiamondSquareInterp::CatmullRom;
    const auto smooth = GenerateFromConfig(config, folder);
    double change = 0.0;
    for (std::size_t row = 0; row < smooth.grids[0].heights.Height(); ++row) {
        for (std::size_t col = 0; col < smooth.grids[0].heights.Width(); ++col) {
            change =
                std::max(change, std::fabs(static_cast<double>(fine(col, row)) -
                                           static_cast<double>(smooth.grids[0].heights(col, row))));
        }
    }
    EXPECT_GT(change, 0.0) << "without displacements the refinement is a smooth spline";
    EXPECT_EQ(smooth.grids[0].heights(0, 0), dem(15, 10));
}

TEST(GenerateFromConfig, ContinuesADemsSpectrumOnlyBelowItsCells) {
    const auto folder = TempPath("terraingen_spectral");
    const auto dem = WriteDem(folder, "dem.tif");
    TerrainGenConfig config = RasterConfig("dem.tif");
    terraingen::SpectralContinuationLayerSpec layer;
    layer.fit.maxPixels = 16.0;
    layer.minWavelength = 1.0;
    config.layers = {layer};
    const GridRegion region{21, 21, 1.25, 1102.5, 1922.5};
    config.outputs = {terraingen::testing::Heightmap("fine.tif", region),
                      terraingen::testing::Heightmap("base.tif", std::nullopt)};
    terraingen::ValidateConfig(config, folder);
    const auto generated = GenerateFromConfig(config, folder);
    ASSERT_EQ(generated.grids.size(), 2u);
    ExpectSameRaster(generated.grids[1].heights, dem);
    const auto plain =
        terraingen::Evaluate(GenerateFromConfig(RasterConfig("dem.tif"), folder).generator,
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
    ExpectSameRaster(GenerateFromConfig(config, folder).grids[0].heights, plain);
}

namespace {

/// A DEM with roughness and noise that estimate their settings from it, over 2.5 m cells.
TerrainGenConfig EstimatingDem(const GridRegion& region) {
    TerrainGenConfig config = RasterConfig("dem.tif");
    terraingen::RoughnessLayerSpec roughness;
    roughness.estimateFromBase = true;
    roughness.roughness.maxWavelength = 4.0;
    roughness.roughness.minWavelength = 0.5;
    terraingen::NoiseLayerSpec noise;
    noise.estimateFromBase = true;
    noise.stack[0].layer.fractal.wavelength = 5.0f;
    noise.stack[0].layer.fractal.octaves = 3;
    config.layers = {noise, roughness};
    config.outputs = {terraingen::testing::Heightmap("fine.tif", region)};
    return config;
}

}  // namespace

TEST(GenerateFromConfig, EstimatesRoughnessAndNoiseFromTheBaseAlikeForGridsTilesAndPoints) {
    const auto folder = TempPath("terraingen_estimate");
    const auto dem = WriteDem(folder, "dem.tif");
    const GridRegion region{24, 20, 2.5, 1080.0, 1950.0};
    const TerrainGenConfig config = EstimatingDem(region);
    terraingen::ValidateConfig(config, folder);
    const auto generated = GenerateFromConfig(config, folder);
    const auto shifted = GenerateFromConfig(EstimatingDem({8, 8, 1.0, 1010.0, 1990.0}), folder);
    const terraingen::RasterSource base(dem);
    const terraingen::BaseRoughness expected = terraingen::EstimateSourceRoughness(base);
    const auto roughness =
        terraingen::LayerAt<terraingen::RoughnessLayer>(generated.generator, 1, "roughness");
    EXPECT_DOUBLE_EQ(roughness->Options().hurst, expected.hurst);
    EXPECT_DOUBLE_EQ(roughness->Options().rmsSlope, expected.RmsSlope(0.25));
    EXPECT_EQ(roughness->Options(),
              terraingen::LayerAt<terraingen::RoughnessLayer>(shifted.generator, 1, "roughness")
                  ->Options());
    const auto noise =
        terraingen::LayerAt<terraingen::NoiseRelief>(generated.generator, 0, "noise");
    EXPECT_GT(noise->Params().layers[0].fractal.amplitude, 0.0f);
    EXPECT_EQ(
        noise->Params(),
        terraingen::LayerAt<terraingen::NoiseRelief>(shifted.generator, 0, "noise")->Params());

    const terraingen::Raster<float>& grid = generated.grids[0].heights;
    const terraingen::RasterGrid lattice = terraingen::RasterGrid::FromRaster(grid);
    const terraingen::Raster<float> tile = terraingen::Evaluate(
        generated.generator, terraingen::Subgrid(lattice, {5, 17, 3, 11}), 2.5);
    std::vector<terraingen::Vec2d> points;
    for (std::size_t row = 0; row < grid.Height(); ++row) {
        for (std::size_t col = 0; col < grid.Width(); ++col) {
            points.push_back(grid.CellCenter(col, row));
        }
    }
    const std::vector<float> atPoints = terraingen::HeightsAt(generated.generator, points, 2.5);
    for (std::size_t row = 0; row < grid.Height(); ++row) {
        for (std::size_t col = 0; col < grid.Width(); ++col) {
            ASSERT_EQ(atPoints[row * grid.Width() + col], grid(col, row)) << col << " " << row;
        }
    }
    for (std::size_t row = 0; row < tile.Height(); ++row) {
        for (std::size_t col = 0; col < tile.Width(); ++col) {
            ASSERT_EQ(tile(col, row), grid(col + 5, row + 3)) << col << " " << row;
        }
    }
}

namespace {

/// `Small` with crater seed 7, a boulder layer of seed 11, crater and boulder lists over its
/// heightmap's region and a report output without a path.
TerrainGenConfig WithReport() {
    TerrainGenConfig config = Small();
    Craters(config).seed = 7;
    terraingen::BoulderLayerSpec boulders;
    boulders.sizes = terraingen::GolombekRappBouldersSpec{};
    boulders.boulders.sizes.coverage = 0.3;
    boulders.boulders.sizes.minDiameter = 1.0;
    boulders.boulders.seed = 11;
    config.layers.push_back(boulders);
    config.outputs.push_back(CraterListOutput{{"", ListFormat::Csv, SmallRegion()}});
    config.outputs.push_back(terraingen::BoulderListOutput{{"", ListFormat::Csv, SmallRegion()}});
    config.outputs.push_back(terraingen::ReportOutput{});
    return config;
}

/// The "grids" of a report holding `grids`, as `ReportToJson` writes them with NaN as null.
nlohmann::json GridsJson(const std::vector<terraingen::GridReport>& grids) {
    terraingen::TerrainReport report;
    report.grids = grids;
    return nlohmann::json::parse(terraingen::ReportToJson(report)).at("grids");
}

}  // namespace

TEST(GenerateFromConfig, ReportsGridStatisticsFeatureCountsTimingsAndSeeds) {
    TerrainGenConfig config = WithReport();
    const auto generated = GenerateFromConfig(config, {}, 2, terraingen::Backend::Cpu);
    ASSERT_TRUE(generated.report);
    const terraingen::TerrainReport& report = *generated.report;
    const terraingen::GeneratedGrid& grid = generated.Grid();
    ASSERT_TRUE(grid.craters);
    ASSERT_TRUE(grid.boulders);
    ASSERT_EQ(report.grids.size(), 1u);
    const auto target = BuildCraterModels(Craters(config)).density;
    const terraingen::GridReport expected = terraingen::ReportGrid(
        grid.heights, terraingen::Surface{}, &*grid.craters, &*grid.boulders, target.get(), 1);
    EXPECT_EQ(GridsJson(report.grids), GridsJson({expected}));

    const terraingen::GridReport& stats = report.grids[0];
    EXPECT_EQ(stats.grid, SmallRegion().Grid());
    EXPECT_LT(stats.heightMin, stats.heightMax);
    EXPECT_GT(stats.slopeMeanDeg, 0.0);
    const double area = stats.grid.SurfaceArea(terraingen::Surface{});
    std::size_t counted = 0;
    std::size_t withExpected = 0;
    for (const terraingen::CraterBin& bin : stats.craterBins) {
        counted += bin.count;
        if (std::isfinite(bin.expected)) {
            ++withExpected;
            EXPECT_DOUBLE_EQ(bin.expected,
                             target->ExpectedCount(area, bin.minDiameter, bin.maxDiameter));
        }
    }
    EXPECT_GT(grid.craters->craters.Size(), 0u);
    EXPECT_EQ(counted, grid.craters->craters.Size());
    EXPECT_GT(withExpected, 0u);
    EXPECT_GT(stats.boulderCount, 0u);
    EXPECT_EQ(stats.boulderCount, grid.boulders->boulders.size());
    EXPECT_GT(stats.boulderCover, 0.0);

    ASSERT_EQ(report.layers.size(), 3u);
    const std::vector<std::string> types{"noise", "craters", "boulders"};
    for (std::size_t i = 0; i < types.size(); ++i) {
        EXPECT_EQ(report.layers[i].type, types[i]);
        EXPECT_GT(report.layers[i].seconds, 0.0) << types[i];
    }
    EXPECT_EQ(report.backend, "cpu");
    EXPECT_EQ(report.threads, 2u);
    EXPECT_EQ(report.seeds, (std::vector<std::uint32_t>{7u, 11u}));
    EXPECT_EQ(report.seconds, generated.seconds);
    EXPECT_EQ(report.version, TERRAINGEN_FULL_VERSION_STRING);
}

TEST(GenerateFromConfig, ReportsOnlyWithAReportOutputAndPlacesNoFeaturesForIt) {
    TerrainGenConfig config = Small();
    EXPECT_FALSE(GenerateFromConfig(config).report) << "no report output";

    const GridRegion wide{33, 25, 4.0};
    CraterLayerSpec inner = Craters(config);
    inner.seed = 5;
    config.layers.push_back(terraingen::MaskedLayerSpec{
        terraingen::CircleRegionSpec{0.0, 0.0, 20.0}, 0.0, terraingen::BlendShape::Linear, inner});
    config.outputs.push_back(terraingen::testing::Heightmap("", wide));
    config.outputs.push_back(CraterListOutput{{"", ListFormat::Csv, GridRegion{40, 40, 1.0}}});
    config.outputs.push_back(terraingen::ReportOutput{});
    const auto generated = GenerateFromConfig(config, {}, 1);
    ASSERT_EQ(generated.grids.size(), 3u);
    ASSERT_TRUE(generated.report);
    const terraingen::TerrainReport& report = *generated.report;
    ASSERT_EQ(report.grids.size(), 2u) << "only the grids with heights";
    EXPECT_EQ(report.grids[0].grid, SmallRegion().Grid());
    EXPECT_EQ(report.grids[1].grid, wide.Grid());
    for (const terraingen::GridReport& grid : report.grids) {
        EXPECT_TRUE(grid.craterBins.empty()) << "no list over the grid, so no craters placed";
        EXPECT_EQ(grid.boulderCount, 0u);
        EXPECT_FALSE(std::isnan(grid.heightMean));
    }
    EXPECT_FALSE(generated.grids[0].craters);
    ASSERT_EQ(report.layers.size(), 3u);
    EXPECT_EQ(report.layers[2].type, "masked (craters)");
    EXPECT_EQ(report.seeds, (std::vector<std::uint32_t>{1u, 5u}))
        << "a masked layer's from its layer";
    EXPECT_EQ(report.threads, 1u);
}
