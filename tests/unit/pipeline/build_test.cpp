/// @file
/// @brief Tests of building library objects from configuration specs (pipeline/build.cpp).
#include "pipeline_configs.h"
#include "terraingen/pipeline/detail/build.h"
#include "terraingen/pipeline/run.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>

using terraingen::BuildCraterModels;
using terraingen::ConfigError;
using terraingen::CraterLayerSpec;
using terraingen::TerrainConfig;
using terraingen::testing::Craters;
using terraingen::testing::Small;

namespace {

/// The JSON path of the `ConfigError` that building `craters` at "layers[0]" throws.
std::string ModelErrorPath(const CraterLayerSpec& craters) {
    try {
        BuildCraterModels(craters, "layers[0]");
    } catch (const ConfigError& error) {
        return error.Path();
    }
    return "built";
}

}  // namespace

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

    craters.density = terraingen::MarsSurfaceSpec{3.0};
    const auto models = BuildCraterModels(craters);
    EXPECT_NEAR(models.density->Cumulative(1000.0), terraingen::IvanovMarsChronology(3.0), 1e-15);
    EXPECT_NE(dynamic_cast<const terraingen::UniformAges*>(models.ages.get()), nullptr);
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

TEST(BuildCraterModels, WearsTheProfileByDiffusion) {
    CraterLayerSpec craters;
    craters.degradation = terraingen::DiffusionDegradationSpec{};
    const auto models = BuildCraterModels(craters);
    ASSERT_NE(dynamic_cast<const terraingen::DiffusedProfile*>(models.profile.get()), nullptr);
    const terraingen::Crater crater{0.0, 0.0, 500.0, 1.0, 1u};
    EXPECT_DOUBLE_EQ(models.degradation->State(crater).diffusion,
                     terraingen::Fassett2022HartmannDiffusivity()(500.0, 1.0) / (500.0 * 500.0));
    for (const auto model : {terraingen::DiffusivityModel::Fassett2022Trask,
                             terraingen::DiffusivityModel::FassettThomson2014,
                             terraingen::DiffusivityModel::FassettThomson2014Calibration,
                             terraingen::DiffusivityModel::PowerLaw}) {
        terraingen::DiffusionDegradationSpec diffusion;
        diffusion.diffusivity = model;
        craters.degradation = diffusion;
        EXPECT_GT(BuildCraterModels(craters).degradation->State(crater).diffusion, 0.0);
    }
}

TEST(BuildCraterModels, ReportsEachModelsProblemsAtItsKey) {
    CraterLayerSpec craters;
    EXPECT_EQ(ModelErrorPath(craters), "built");
    craters.density = terraingen::LunarSurfaceSpec{9.0};
    EXPECT_EQ(ModelErrorPath(craters), "layers[0].density");
    craters.density = terraingen::PowerLawSpec{};
    craters.ages = terraingen::UniformAgesSpec{0.0};
    EXPECT_EQ(ModelErrorPath(craters), "layers[0].ages");
    craters.ages = terraingen::UniformAgesSpec{};
    craters.degradation = terraingen::FreshnessDegradationSpec{0.5};
    EXPECT_EQ(ModelErrorPath(craters), "layers[0].degradation");
    terraingen::DiffusionDegradationSpec diffusion;
    diffusion.diffusivity = terraingen::DiffusivityModel::PowerLaw;
    diffusion.powerLaw = {0.0, 1.0, 0.0};
    craters.degradation = diffusion;
    EXPECT_EQ(ModelErrorPath(craters), "layers[0].degradation");
    craters.body = terraingen::CraterBody::Mars;
    craters.degradation = terraingen::DiffusionDegradationSpec{};
    EXPECT_EQ(ModelErrorPath(craters), "layers[0].degradation.diffusivity");
    craters.degradation = terraingen::NoDegradationSpec{};
    craters.density = terraingen::LunarSurfaceSpec{};
    EXPECT_EQ(ModelErrorPath(craters), "layers[0].density");
    craters.density = terraingen::MarsSurfaceSpec{};
    craters.ages = terraingen::SteadyStateAgesSpec{};
    EXPECT_EQ(ModelErrorPath(craters), "layers[0].ages");
}

TEST(BuildBoulderOptions, BuildsTheSizesAndReportsThemAtTheirKeys) {
    terraingen::BoulderLayerSpec spec;
    EXPECT_EQ(terraingen::BuildBoulderOptions(spec, "layers[3]").distribution, nullptr)
        << "golombek_rapp sizes come from the layer";
    spec.sizes = terraingen::PowerLawSpec{0.001, 2.5};
    spec.ejectaSizes = terraingen::Sp8023CraterBouldersSpec{};
    const auto options = terraingen::BuildBoulderOptions(spec, "layers[3]");
    ASSERT_NE(options.distribution, nullptr);
    EXPECT_DOUBLE_EQ(options.distribution->Cumulative(2.0), 0.001 * std::pow(2.0, -2.5));
    EXPECT_NE(options.ejecta.distribution, nullptr);
    spec.ejectaSizes = terraingen::TabulatedSpec{{1.0}, {0.01}};
    try {
        terraingen::BuildBoulderOptions(spec, "layers[3]");
        FAIL() << "expected a ConfigError";
    } catch (const ConfigError& error) {
        EXPECT_EQ(error.Path(), "layers[3].ejecta_sizes");
    }
}

TEST(BuildRegionShape, BuildsRectanglesAndCircles) {
    const auto rectangle =
        terraingen::BuildRegionShape(terraingen::RectangleRegionSpec{-1.0, -2.0, 3.0, 4.0});
    EXPECT_EQ(rectangle.DistanceOutside(2.5, 3.5), 0.0);
    EXPECT_DOUBLE_EQ(rectangle.DistanceOutside(3.5, 0.0), 0.5);
    const auto circle = terraingen::BuildRegionShape(terraingen::CircleRegionSpec{10.0, 0.0, 2.0});
    EXPECT_EQ(circle.DistanceOutside(11.0, 1.0), 0.0);
    EXPECT_DOUBLE_EQ(circle.DistanceOutside(12.5, 0.0), 0.5);
}

TEST(Resolve, ResolvesRelativePathsAgainstAFolder) {
    EXPECT_EQ(terraingen::Resolve("a.tif", "/data"), std::filesystem::path("/data/a.tif"));
    EXPECT_EQ(terraingen::Resolve("/abs/a.tif", "/data"), std::filesystem::path("/abs/a.tif"));
    EXPECT_EQ(terraingen::Resolve("a.tif", {}), std::filesystem::path("a.tif"));
}

TEST(IsMeshFile, AcceptsGlbAndPly) {
    EXPECT_TRUE(terraingen::IsMeshFile("out/b.glb"));
    EXPECT_TRUE(terraingen::IsMeshFile("b.ply"));
    EXPECT_FALSE(terraingen::IsMeshFile("b.obj"));
    EXPECT_FALSE(terraingen::IsMeshFile(""));
}

TEST(BuildTerrain, BuildsEveryLayerKindInOrder) {
    TerrainConfig config = Small();
    terraingen::BoulderLayerSpec boulders;
    boulders.boulders.minDiameter = 1.0;
    boulders.boulders.ejecta.minCraterDiameter = 10.0;
    terraingen::SecondaryLayerSpec secondaries;
    secondaries.secondaries.onsetCount = 5.0;
    config.layers.push_back(boulders);
    config.layers.push_back(secondaries);
    config.layers.push_back(terraingen::RoughnessLayerSpec{});
    config.layers.push_back(terraingen::BedformLayerSpec{});
    config.layers.push_back(terraingen::ScaleLayerSpec{2.0, 1.0});
    config.layers.push_back(terraingen::ClampLayerSpec{0.0, 1.0});
    config.layers.push_back(terraingen::RegionLayerSpec{terraingen::CircleRegionSpec{0, 0, 10}});
    config.filters = {terraingen::RimSmoothing{}};
    const terraingen::Terrain terrain = terraingen::BuildTerrain(config);
    ASSERT_EQ(terrain.layers.size(), 9u);
    EXPECT_NE(dynamic_cast<const terraingen::NoiseRelief*>(terrain.layers[0].get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::CraterLayer*>(terrain.layers[1].get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::BoulderLayer*>(terrain.layers[2].get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::SecondaryCraterLayer*>(terrain.layers[3].get()),
              nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::RoughnessLayer*>(terrain.layers[4].get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::BedformLayer*>(terrain.layers[5].get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::ScaleLayer*>(terrain.layers[6].get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::ClampLayer*>(terrain.layers[7].get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::RegionLayer*>(terrain.layers[8].get()), nullptr);
    ASSERT_EQ(terrain.filters.size(), 1u);
    TerrainConfig narrow;
    narrow.source = terraingen::FlatSourceSpec{1};
    EXPECT_THROW(terraingen::BuildTerrain(narrow), ConfigError);
}

TEST(BuildAlbedo, BrightensEveryOrOneCraterLayer) {
    TerrainConfig both = terraingen::testing::WithAlbedo();
    both.layers.push_back(Craters(both));
    std::get<terraingen::CraterAlbedoSpec>(both.albedo->layers[0]).craterLayer.reset();
    EXPECT_EQ(terraingen::BuildAlbedo(both, terraingen::BuildTerrain(both)).layers.size(), 2u);
    std::get<terraingen::CraterAlbedoSpec>(both.albedo->layers[0]).craterLayer = 1;
    EXPECT_EQ(terraingen::BuildAlbedo(both, terraingen::BuildTerrain(both)).layers.size(), 1u);
    TerrainConfig none = Small();
    EXPECT_THROW(terraingen::BuildAlbedo(none, terraingen::BuildTerrain(none)), ConfigError);
    TerrainConfig invalid = terraingen::testing::WithAlbedo();
    const terraingen::Terrain terrain = terraingen::BuildTerrain(invalid);
    std::get<terraingen::SyntheticAlbedoSpec>(invalid.albedo->source).value = 2.0;
    EXPECT_THROW(terraingen::BuildAlbedo(invalid, terrain), ConfigError);
}
