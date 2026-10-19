#include "configs.h"
#include "support/temp_path.h"
#include "terraingen/config/build.h"
#include "terraingen/config/generate.h"
#include "terraingen/mesh/io/icq.h"
#include "terraingen/mesh/io/mesh_io.h"
#include "terraingen/mesh/polyhedra.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

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

TEST(BuildCraterModels, ShapesCratersWithTheirMorphometry) {
    CraterLayerSpec craters;
    craters.profile = terraingen::ComplexProfileSpec{};
    const auto lunar = BuildCraterModels(craters);
    craters.morphometry = terraingen::CraterMorphometryFit::Mars;
    const auto mars = BuildCraterModels(craters);
    const terraingen::Crater crater{0.0, 0.0, 10000.0, 0.0, 1u};
    EXPECT_FALSE(lunar.profile->IsComplex(10000.0));
    EXPECT_TRUE(mars.profile->IsComplex(10000.0));
    EXPECT_EQ(mars.degradation->State(crater).fresh,
              terraingen::MarsFreshCraterMorphometry(10000.0));
    EXPECT_EQ(lunar.degradation->State(crater).fresh,
              terraingen::LunarFreshCraterMorphometry(10000.0));
}

TEST(BuildCraterModels, NoDegradationTakesTheDepthRatioOfTheMorphometry) {
    CraterLayerSpec craters;
    craters.degradation = terraingen::NoDegradationSpec{};
    const terraingen::Crater crater{0.0, 0.0, 5000.0, 0.0, 1u};
    const double lunar = BuildCraterModels(craters).degradation->State(crater).current.depthRatio;
    craters.morphometry = terraingen::CraterMorphometryFit::Mars;
    const double mars = BuildCraterModels(craters).degradation->State(crater).current.depthRatio;
    EXPECT_DOUBLE_EQ(lunar, terraingen::LunarFreshCraterMorphometry(5000.0).depthRatio);
    EXPECT_DOUBLE_EQ(mars, terraingen::MarsFreshCraterMorphometry(5000.0).depthRatio);
    EXPECT_NE(lunar, mars);
}

TEST(BuildCraterModels, BuildsAProductionSurfaceForEachProductionAndChronology) {
    using terraingen::ChronologyModel;
    using terraingen::ProductionFunction;
    CraterLayerSpec craters;
    for (const auto production : {ProductionFunction::Neukum, ProductionFunction::ExtendedNeukum,
                                  ProductionFunction::Ivanov}) {
        for (const auto chronology : {ChronologyModel::Neukum, ChronologyModel::Ivanov}) {
            craters.density = terraingen::ProductionSurfaceSpec{production, chronology, 3.0};
            const auto models = BuildCraterModels(craters, "layers[0]");
            const double expected = chronology == ChronologyModel::Ivanov
                                        ? terraingen::IvanovMarsChronology(3.0)
                                        : terraingen::NeukumLunarChronology(3.0);
            EXPECT_NEAR(models.density->Cumulative(1000.0) / expected, 1.0, 1e-12)
                << static_cast<int>(production) << " " << static_cast<int>(chronology);
        }
    }
}

TEST(BuildCraterModels, ProductionSurfacesKeepTheirProductionsShape) {
    using terraingen::ProductionFunction;
    CraterLayerSpec craters;
    const auto ratio = [&](ProductionFunction production) {
        craters.density = terraingen::ProductionSurfaceSpec{production};
        const auto density = BuildCraterModels(craters).density;
        return density->Cumulative(100.0) / density->Cumulative(1000.0);
    };
    EXPECT_NEAR(ratio(ProductionFunction::Neukum),
                terraingen::NeukumProductionFunction().Cumulative(100.0) /
                    terraingen::NeukumProductionFunction().Cumulative(1000.0),
                1e-9);
    EXPECT_NEAR(ratio(ProductionFunction::Ivanov),
                terraingen::IvanovMarsProductionFunction().Cumulative(100.0) /
                    terraingen::IvanovMarsProductionFunction().Cumulative(1000.0),
                1e-9);
    craters.density = terraingen::ProductionSurfaceSpec{ProductionFunction::ExtendedNeukum};
    EXPECT_GT(BuildCraterModels(craters).density->Cumulative(5.0), 0.0);
    craters.density = terraingen::ProductionSurfaceSpec{ProductionFunction::Neukum};
    EXPECT_THROW(BuildCraterModels(craters).density->Cumulative(5.0), std::domain_error);
}

TEST(BuildCraterModels, MinimumIsTheLeastOfItsDistributions) {
    CraterLayerSpec craters;
    const terraingen::ProductionSurfaceSpec production{};
    craters.density = production;
    const auto surface = BuildCraterModels(craters).density;
    craters.density = terraingen::MinimumSpec{{production, terraingen::TraskSpec{}}};
    const auto minimum = BuildCraterModels(craters).density;
    const auto trask = terraingen::TraskEquilibrium();
    for (const double d : {5.0, 50.0, 500.0, 5000.0}) {
        EXPECT_DOUBLE_EQ(minimum->Cumulative(d),
                         std::min(surface->Cumulative(d), trask.Cumulative(d)))
            << d;
    }
    EXPECT_LT(trask.Cumulative(5.0), surface->Cumulative(5.0));
    EXPECT_LT(surface->Cumulative(5000.0), trask.Cumulative(5000.0));
}

TEST(BuildCraterModels, AnEmptyMinimumIsAnErrorAtTheDensity) {
    CraterLayerSpec craters;
    craters.density = terraingen::MinimumSpec{};
    EXPECT_EQ(ModelErrorPath(craters), "layers[0].density");
}

TEST(BuildCraterModels, BuildsSteadyStateAgesFromEachModel) {
    using terraingen::ChronologyModel;
    using terraingen::EquilibriumModel;
    using terraingen::ProductionFunction;
    using terraingen::SteadyStateAgesSpec;
    CraterLayerSpec craters;
    const auto lifetime = [&](SteadyStateAgesSpec spec, double diameter) {
        craters.ages = spec;
        return BuildCraterModels(craters).ages->LifetimeGyr(diameter);
    };
    const double trask =
        lifetime({3.5, ProductionFunction::ExtendedNeukum, EquilibriumModel::Trask}, 20.0);
    const double hartmann =
        lifetime({3.5, ProductionFunction::ExtendedNeukum, EquilibriumModel::Hartmann}, 20.0);
    EXPECT_GT(trask, 0.0);
    EXPECT_GT(hartmann, 0.0);
    EXPECT_NE(trask, hartmann);

    const double extended = lifetime({}, 50.0);
    const double neukum = lifetime({3.5, ProductionFunction::Neukum}, 50.0);
    EXPECT_GT(neukum, 0.0);
    EXPECT_NEAR(neukum / extended, 1.0, 0.05);
    EXPECT_THROW(lifetime({3.5, ProductionFunction::Neukum}, 5.0), std::domain_error);
    EXPECT_GT(lifetime({}, 5.0), 0.0);

    const SteadyStateAgesSpec ivanov{3.0, ProductionFunction::Ivanov, EquilibriumModel::Trask,
                                     ChronologyModel::Ivanov};
    craters.ages = ivanov;
    const auto models = BuildCraterModels(craters);
    const auto& ages = dynamic_cast<const terraingen::SteadyStateAges&>(*models.ages);
    EXPECT_EQ(ages.SurfaceAgeGyr(), 3.0);
    EXPECT_GT(ages.LifetimeGyr(100.0), 0.0);
    EXPECT_LE(ages.LifetimeGyr(100.0), 3.0);
}

TEST(BuildCraterModels, BuildsEachSelectedModel) {
    CraterLayerSpec craters;
    auto models = BuildCraterModels(craters);
    EXPECT_NE(dynamic_cast<const terraingen::SteadyStateAges*>(models.ages.get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::DiffusionDegradation*>(models.degradation.get()),
              nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::DiffusedProfile*>(models.profile.get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::MinimumSfd*>(models.density.get()), nullptr);
    EXPECT_LT(models.density->Cumulative(4.0), terraingen::TraskEquilibrium().Cumulative(4.0))
        << "below Trask equilibrium";

    craters.ages = terraingen::UniformAgesSpec{};
    craters.degradation = terraingen::FreshnessDegradationSpec{};
    models = BuildCraterModels(craters);
    EXPECT_NE(dynamic_cast<const terraingen::UniformAges*>(models.ages.get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::FreshnessDegradation*>(models.degradation.get()),
              nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::MartinParkesDunstanProfile*>(models.profile.get()),
              nullptr);

    craters.density = terraingen::PowerLawSpec{0.2, 2.5};
    craters.degradation = terraingen::NoDegradationSpec{};
    craters.profile = terraingen::FassettThomsonSpec{};
    models = BuildCraterModels(craters);
    EXPECT_DOUBLE_EQ(models.density->Cumulative(2.0), 0.2 * std::pow(2.0, -2.5));
    EXPECT_NE(dynamic_cast<const terraingen::UniformAges*>(models.ages.get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::NoDegradation*>(models.degradation.get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::FassettThomsonProfile*>(models.profile.get()),
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
    EXPECT_DOUBLE_EQ(
        density(terraingen::PiecewiseUniformSpec{{{10.0, 100.0, 1.0}, {20.0, 40.0, 3.0}}, 0.01}),
        0.0025 * 50.0 / 90.0);

    craters.ages = terraingen::FreshAgesSpec{};
    EXPECT_NE(dynamic_cast<const terraingen::FreshAges*>(BuildCraterModels(craters).ages.get()),
              nullptr);
    craters.ages = terraingen::SteadyStateAgesSpec{};
    const auto extended = BuildCraterModels(craters);
    EXPECT_GT(extended.ages->LifetimeGyr(5.0), 0.0);
    craters.ages = terraingen::SteadyStateAgesSpec{3.0, terraingen::ProductionFunction::Neukum,
                                                   terraingen::EquilibriumModel::Hartmann};
    const auto steady = BuildCraterModels(craters);
    EXPECT_EQ(dynamic_cast<const terraingen::SteadyStateAges&>(*steady.ages).SurfaceAgeGyr(), 3.0);
    EXPECT_THROW(steady.ages->LifetimeGyr(5.0), std::domain_error);
    craters.degradation = terraingen::NoDegradationSpec{0.18};
    EXPECT_EQ(BuildCraterModels(craters).degradation->State({0, 0, 10, 0, 1}).current.depthRatio,
              0.18);
}

TEST(BuildCraterModels, WearsTheProfileByDiffusion) {
    CraterLayerSpec craters;
    craters.ages = terraingen::UniformAgesSpec{};
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

TEST(BuildCraterModels, MatchesTheDiffusivityToTheSteadyStateEquilibrium) {
    CraterLayerSpec craters;
    craters.degradation = terraingen::DiffusionDegradationSpec{};
    std::vector<double> diffusion;
    for (const auto equilibrium :
         {terraingen::EquilibriumModel::Trask, terraingen::EquilibriumModel::Hartmann}) {
        terraingen::SteadyStateAgesSpec steady;
        steady.equilibrium = equilibrium;
        craters.ages = steady;
        const auto models = BuildCraterModels(craters);
        const auto& ages = dynamic_cast<const terraingen::SteadyStateAges&>(*models.ages);
        for (const double diameter : {3.0, 8.0, 40.0, 150.0}) {
            const double lifetime = ages.EquilibriumLifetimeGyr(diameter);
            const auto state = [&](double age) {
                return models.degradation->State({0.0, 0.0, diameter, age, 1u});
            };
            EXPECT_NEAR(state(lifetime).current.depthRatio, terraingen::kVisibilityDepthRatio, 1e-3)
                << diameter;
            EXPECT_GT(state(0.5 * lifetime).current.depthRatio, terraingen::kVisibilityDepthRatio);
            EXPECT_DOUBLE_EQ(state(0.5 * lifetime).diffusion, 0.5 * state(lifetime).diffusion);
        }
        diffusion.push_back(models.degradation->State({0.0, 0.0, 40.0, 0.1, 1u}).diffusion);
    }
    EXPECT_LT(diffusion[0], diffusion[1]) << "craters last longer at Trask's density";
}

TEST(BuildCraterModels, ReportsEachModelsProblemsAtItsKey) {
    CraterLayerSpec craters;
    EXPECT_EQ(ModelErrorPath(craters), "built");
    craters.density = terraingen::ProductionSurfaceSpec{
        terraingen::ProductionFunction::ExtendedNeukum, terraingen::ChronologyModel::Neukum, 9.0};
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
    EXPECT_EQ(ModelErrorPath(craters), "built") << "ValidateConfig checks the diffusivity";
    craters.degradation = terraingen::NoDegradationSpec{};
    craters.ages = terraingen::SteadyStateAgesSpec{9.0};
    EXPECT_EQ(ModelErrorPath(craters), "layers[0].ages");
}

TEST(BuildField, BuildsConstantsAndReadsGeoTiffs) {
    EXPECT_EQ(terraingen::config::BuildField({.value = 4.0}, "f")->ValueAt(1.0, 2.0), 4.0);
    terraingen::Raster<float> raster(2, 2, 3.0f);
    raster.SetTransform(terraingen::GeoTransform::NorthUp(0.0, 2.0, 1.0));
    const auto path = terraingen::testing::TempPath("build_field.tif");
    terraingen::WriteGeoTiff(path, raster, {{1, 1, 0, 1, 1024, 0, 1, 1}, {}, {}});
    const auto field = terraingen::config::BuildField(
        {.path = path.filename().string(), .scale = 2.0, .fallback = -1.0}, "f",
        path.parent_path());
    EXPECT_DOUBLE_EQ(field->ValueAt(1.0, 1.0), 6.0);
    EXPECT_DOUBLE_EQ(field->ValueAt(9.0, 9.0), -1.0);
    try {
        terraingen::config::BuildField({.path = "missing.tif"}, "layers[1].completeness");
        FAIL() << "expected a ConfigError";
    } catch (const ConfigError& error) {
        EXPECT_EQ(error.Path(), "layers[1].completeness");
    }
    EXPECT_THROW(terraingen::config::BuildField(
                     {.path = path.string(), .method = terraingen::ResampleMethod::Cubic}, "f"),
                 ConfigError);
}

TEST(BuildDensityMap, BuildsTheFieldAndLeavesTheMapUnchecked) {
    const terraingen::DensityMap map =
        terraingen::config::BuildDensityMap({.field = {.value = 5.0},
                                             .measure = terraingen::DensityMeasure::Count,
                                             .minDiameter = 4.5,
                                             .areaUnit = 1e6,
                                             .total = false},
                                            "m");
    EXPECT_EQ(map.field->ValueAt(0.0, 0.0), 5.0);
    EXPECT_EQ(map.minDiameter, 4.5);
    EXPECT_TRUE(std::isinf(map.maxDiameter));
    EXPECT_EQ(map.areaUnit, 1e6);
    EXPECT_FALSE(map.total);
    EXPECT_NO_THROW(terraingen::config::BuildDensityMap(
        {.measure = terraingen::DensityMeasure::Count}, "layers[0].density_map"));
    try {
        terraingen::config::BuildDensityMap({.field = {.path = "missing.tif"}},
                                            "layers[0].density_map");
        FAIL() << "expected a ConfigError";
    } catch (const ConfigError& error) {
        EXPECT_EQ(error.Path(), "layers[0].density_map.field");
    }
}

TEST(ValidateConfig, ReportsDensityAndSlopeMapProblemsAtTheMap) {
    TerrainConfig config = terraingen::testing::Small();
    terraingen::testing::Craters(config).densityMap =
        terraingen::DensityMapSpec{.measure = terraingen::DensityMeasure::Count};
    EXPECT_EQ(terraingen::testing::ValidationErrorPath(config), "layers[1].density_map");
    config = terraingen::testing::Small();
    terraingen::BoulderLayerSpec boulders;
    boulders.sizes = terraingen::PowerLawSpec{0.01, 3.0};
    boulders.slopeMap = terraingen::SizeSlopeMapSpec{.field = {.value = 2.5}};
    boulders.slopeMap->maxExponent = boulders.slopeMap->minExponent;
    config.layers.push_back(boulders);
    EXPECT_EQ(terraingen::testing::ValidationErrorPath(config), "layers[2].slope_map");
}

TEST(BuildBoulderOptions, BuildsTheSizesAndReportsThemAtTheirKeys) {
    terraingen::BoulderLayerSpec spec;
    const auto nominal = terraingen::config::BuildBoulderOptions(spec, "layers[3]");
    ASSERT_NE(nominal.sizes.distribution, nullptr);
    const terraingen::TabulatedSfd sp8023 =
        terraingen::Sp8023IntercraterBlocks(terraingen::SpecificationCurve::Nominal);
    EXPECT_DOUBLE_EQ(nominal.sizes.distribution->Cumulative(1.0), sp8023.Cumulative(1.0))
        << "the nominal SP-8023 intercrater blocks by default";
    EXPECT_EQ(nominal.sizes.maxDiameter, sp8023.ValidRange().max);
    EXPECT_NEAR(nominal.sizes.maxDiameter, 5.86, 0.01);
    double cover = 0.0;
    constexpr int kSteps = 2000;
    const double step = std::log(nominal.sizes.maxDiameter) / kSteps;
    for (int i = 0; i < kSteps; ++i) {
        const double d = std::exp((i + 0.5) * step);
        cover +=
            0.25 * std::numbers::pi * d * d * nominal.sizes.distribution->Incremental(d) * d * step;
    }
    EXPECT_GT(cover, 0.003) << "rocks of 1 m and more cover the lunar mean or more";
    EXPECT_LT(cover, 0.005) << "and no more than the maria mean";
    spec.sizes = terraingen::GolombekRappBouldersSpec{};
    EXPECT_EQ(terraingen::config::BuildBoulderOptions(spec, "layers[3]").sizes.distribution,
              nullptr)
        << "golombek_rapp sizes come from the layer";
    spec.sizes = terraingen::PowerLawSpec{0.001, 2.5};
    spec.ejectaSizes = terraingen::Sp8023CraterBouldersSpec{};
    const auto options = terraingen::config::BuildBoulderOptions(spec, "layers[3]");
    ASSERT_NE(options.sizes.distribution, nullptr);
    EXPECT_DOUBLE_EQ(options.sizes.distribution->Cumulative(2.0), 0.001 * std::pow(2.0, -2.5));
    EXPECT_NE(options.ejecta.distribution, nullptr);
    spec.sizes = terraingen::PiecewiseUniformSpec{{{0.5, 1.5, 0.02}}};
    EXPECT_DOUBLE_EQ(terraingen::config::BuildBoulderOptions(spec, "layers[3]")
                         .sizes.distribution->Cumulative(1.0),
                     0.01);
    spec.ejectaSizes = terraingen::TabulatedSpec{{1.0}, {0.01}};
    try {
        terraingen::config::BuildBoulderOptions(spec, "layers[3]");
        FAIL() << "expected a ConfigError";
    } catch (const ConfigError& error) {
        EXPECT_EQ(error.Path(), "layers[3].ejecta_sizes");
    }
}

TEST(BuildRegionShape, BuildsRectanglesAndCircles) {
    const auto rectangle =
        terraingen::config::BuildRegionShape(terraingen::RectangleRegionSpec{-1.0, -2.0, 3.0, 4.0});
    EXPECT_EQ(rectangle.DistanceOutside(2.5, 3.5), 0.0);
    EXPECT_DOUBLE_EQ(rectangle.DistanceOutside(3.5, 0.0), 0.5);
    const auto circle =
        terraingen::config::BuildRegionShape(terraingen::CircleRegionSpec{10.0, 0.0, 2.0});
    EXPECT_EQ(circle.DistanceOutside(11.0, 1.0), 0.0);
    EXPECT_DOUBLE_EQ(circle.DistanceOutside(12.5, 0.0), 0.5);
}

TEST(Resolve, ResolvesRelativePathsAgainstAFolder) {
    EXPECT_EQ(terraingen::config::Resolve("a.tif", "/data"), std::filesystem::path("/data/a.tif"));
    EXPECT_EQ(terraingen::config::Resolve("/abs/a.tif", "/data"),
              std::filesystem::path("/abs/a.tif"));
    EXPECT_EQ(terraingen::config::Resolve("a.tif", {}), std::filesystem::path("a.tif"));
}

TEST(BuildTerrain, BoulderEjectaComeFromEveryCraterLayerBefore) {
    TerrainConfig config;
    config.outputs = {terraingen::testing::Heightmap("e.tif", terraingen::GridRegionSpec{65, 65})};
    CraterLayerSpec first;
    first.generate = false;
    first.ages = terraingen::FreshAgesSpec{};
    first.manual = {{0.0, 0.0, 40.0, 0.0, 1u, true}};
    CraterLayerSpec second = first;
    second.manual = {{500.0, 0.0, 40.0, 0.0, 2u, true}};
    terraingen::BoulderLayerSpec boulders;
    boulders.boulders.ejecta.minCraterDiameter = 10.0;
    config.layers = {first, second, boulders};
    const terraingen::Terrain terrain = terraingen::BuildTerrain(config);
    const auto& layer = dynamic_cast<const terraingen::BoulderLayer&>(*terrain.layers[2]);
    EXPECT_EQ(layer.AbundanceAt(20.0, 0.0).ejecta, 1.0) << "the first crater layer's rim";
    EXPECT_EQ(layer.AbundanceAt(520.0, 0.0).ejecta, 1.0) << "the second crater layer's rim";
}

TEST(BuildTerrain, BuildsEveryLayerKindInOrder) {
    TerrainConfig config = Small();
    terraingen::BoulderLayerSpec boulders;
    boulders.boulders.sizes.minDiameter = 1.0;
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
    config.layers.push_back(terraingen::DuneLayerSpec{terraingen::MarsBarchans()});
    const terraingen::Terrain terrain = terraingen::BuildTerrain(config);
    ASSERT_EQ(terrain.layers.size(), 10u);
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
    EXPECT_NE(dynamic_cast<const terraingen::DuneLayer*>(terrain.layers[9].get()), nullptr);
    TerrainConfig narrow;
    narrow.outputs = {terraingen::testing::Heightmap("n.tif", terraingen::GridRegionSpec{1})};
    EXPECT_THROW(terraingen::BuildTerrain(narrow), ConfigError);
}

TEST(BuildAlbedo, BrightensEveryOrOneCraterLayer) {
    TerrainConfig both = terraingen::testing::WithAlbedo();
    both.layers.push_back(Craters(both));
    std::get<terraingen::CraterAlbedoSpec>(both.albedo->layers[0]).craterLayer.reset();
    EXPECT_EQ(terraingen::BuildAlbedo(both, terraingen::BuildTerrain(both)).layers.size(), 2u);
    std::get<terraingen::CraterAlbedoSpec>(both.albedo->layers[0]).craterLayer = 1;
    EXPECT_EQ(terraingen::BuildAlbedo(both, terraingen::BuildTerrain(both)).layers.size(), 1u);
    both.albedo->layers.push_back(terraingen::SplotchAlbedoSpec{});
    const terraingen::Albedo spotted =
        terraingen::BuildAlbedo(both, terraingen::BuildTerrain(both));
    ASSERT_EQ(spotted.layers.size(), 2u);
    EXPECT_NE(dynamic_cast<const terraingen::SplotchAlbedo*>(spotted.layers[1].get()), nullptr);
    TerrainConfig rocky = both;
    rocky.layers.push_back(terraingen::BoulderLayerSpec{});
    rocky.layers.push_back(terraingen::BoulderLayerSpec{});
    rocky.albedo->layers = {terraingen::RockAlbedoSpec{}, terraingen::SlopeAlbedoSpec{}};
    const terraingen::Albedo rocks =
        terraingen::BuildAlbedo(rocky, terraingen::BuildTerrain(rocky));
    ASSERT_EQ(rocks.layers.size(), 2u);
    EXPECT_NE(dynamic_cast<const terraingen::RockAlbedo*>(rocks.layers[0].get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::SlopeAlbedo*>(rocks.layers[1].get()), nullptr);
    const terraingen::RasterGrid grid{8, 8, terraingen::GeoTransform::NorthUp(0.0, 8.0, 1.0)};
    EXPECT_EQ(terraingen::EvaluateAlbedo(rocks, grid, {.spacing = 1.0})[0].Width(), 8u)
        << "the rock's grounds outlive the terrain they were built from";
    TerrainConfig none = Small();
    EXPECT_THROW(terraingen::BuildAlbedo(none, terraingen::BuildTerrain(none)), ConfigError);
    TerrainConfig invalid = terraingen::testing::WithAlbedo();
    const terraingen::Terrain terrain = terraingen::BuildTerrain(invalid);
    std::get<terraingen::SyntheticAlbedoSpec>(invalid.albedo->source).value = 2.0;
    EXPECT_THROW(terraingen::BuildAlbedo(invalid, terrain), ConfigError);
}

namespace {

/// The JSON path of the `ConfigError` that building `spec` from the test directory throws.
std::string SurfaceErrorPath(const terraingen::SurfaceSpec& spec) {
    try {
        terraingen::BuildSurface(spec, ::testing::TempDir());
    } catch (const ConfigError& error) {
        return error.Path();
    }
    return "built";
}

/// A lumpy body of mean radius about 400 m as a radial shape.
terraingen::RadialShape Lumpy(std::size_t q) {
    return terraingen::RadialShape::Sample(
        q, [](const terraingen::Vec3d& u) { return 400.0 + 30.0 * u.x * u.y - 20.0 * u.z; });
}

}  // namespace

TEST(BuildSurface, LoadsAnIcqShapeModelOnceRelativeToTheConfig) {
    const auto path = terraingen::testing::TempPath("config_shape.icq");
    terraingen::WriteIcq(path, terraingen::ToIcq(Lumpy(12)));
    const terraingen::ShapeModelSurfaceSpec spec{"config_shape.icq"};
    const terraingen::Surface surface = terraingen::BuildSurface(spec, ::testing::TempDir());
    ASSERT_NE(surface.Shape(), nullptr);
    EXPECT_EQ(surface.Shape()->Q(), 12u);
    EXPECT_NEAR(surface.Shape()->At({1.0, 0.0, 0.0}), 400.0, 1e-5);
    EXPECT_EQ(terraingen::BuildSurface(spec, ::testing::TempDir()), surface) << "loaded once";

    const terraingen::Surface coarse = terraingen::BuildSurface(
        terraingen::ShapeModelSurfaceSpec{"config_shape.icq", 6}, ::testing::TempDir());
    EXPECT_EQ(coarse.Shape()->Q(), 6u);
    EXPECT_NEAR(coarse.Shape()->At({0.0, 0.0, 1.0}), 380.0, 0.5);
    const terraingen::Surface metres = terraingen::BuildSurface(
        terraingen::ShapeModelSurfaceSpec{"config_shape.icq", {}, 1.0}, ::testing::TempDir());
    EXPECT_NEAR(metres.Shape()->At({1.0, 0.0, 0.0}), 0.4, 1e-8) << "in metres, not kilometres";
}

TEST(BuildSurface, ResamplesAMeshShapeModelScaledToMetres) {
    terraingen::SceneMesh scene;
    scene.mesh = terraingen::Scaled(terraingen::Icosphere(4), {0.3, 0.2, 0.1});
    const auto path = terraingen::testing::TempPath("config_shape.obj");
    terraingen::WriteMesh(path, scene);
    const terraingen::Surface surface =
        terraingen::BuildSurface(terraingen::ShapeModelSurfaceSpec{path.string(), 16, 1000.0});
    EXPECT_EQ(surface.Shape()->Q(), 16u);
    EXPECT_NEAR(surface.Shape()->At({1.0, 0.0, 0.0}), 300.0, 0.5);
    EXPECT_NEAR(surface.Shape()->At({0.0, 0.0, -1.0}), 100.0, 0.5);
}

TEST(BuildSurface, ReportsShapeModelProblemsAtTheirKeys) {
    EXPECT_EQ(SurfaceErrorPath(terraingen::ShapeModelSurfaceSpec{}), "body.path");
    EXPECT_EQ(SurfaceErrorPath(terraingen::ShapeModelSurfaceSpec{"missing.icq"}), "body.path");
    EXPECT_EQ(SurfaceErrorPath(terraingen::ShapeModelSurfaceSpec{"missing.icq", 0}), "body.q");
    EXPECT_EQ(SurfaceErrorPath(terraingen::ShapeModelSurfaceSpec{"missing.icq", 5000}), "body.q");
    EXPECT_EQ(SurfaceErrorPath(terraingen::ShapeModelSurfaceSpec{"missing.icq", {}, -1.0}),
              "body.metres_per_unit");
    terraingen::SceneMesh off;
    off.mesh = terraingen::Icosphere(1);
    for (terraingen::Vec3d& p : off.mesh.positions) {
        p.x += 5.0;
    }
    terraingen::WriteMesh(terraingen::testing::TempPath("config_off_centre.obj"), off);
    EXPECT_EQ(SurfaceErrorPath(terraingen::ShapeModelSurfaceSpec{"config_off_centre.obj", 4}),
              "body.path");
    const auto corrupt = terraingen::testing::TempPath("config_corrupt.icq");
    std::ofstream(corrupt) << "not an ICQ model\n";
    EXPECT_EQ(SurfaceErrorPath(terraingen::ShapeModelSurfaceSpec{"config_corrupt.icq"}),
              "body.path");
}

TEST(BuildTerrain, GivesLayersTheirMapsCatalogsAndCompleteness) {
    TerrainConfig config = Small();
    auto& craters = std::get<CraterLayerSpec>(config.layers.at(1));
    craters.densityMap = terraingen::DensityMapSpec{.field = {.value = 2.0}};
    craters.completeness = terraingen::FieldSpec{.value = 50.0};
    terraingen::BoulderLayerSpec boulders;
    boulders.densityMap = terraingen::DensityMapSpec{.field = {.value = 1.5}};
    boulders.manual = {{1.0, 2.0, 3.0, 0.0, 0u}};
    boulders.completeness = terraingen::FieldSpec{.value = 2.0};
    boulders.sizes = terraingen::PowerLawSpec{0.01, 3.0};
    boulders.slopeMap = terraingen::SizeSlopeMapSpec{.field = {.value = 2.5}};
    config.layers.push_back(boulders);
    const terraingen::Terrain terrain = terraingen::BuildTerrain(config);
    const auto* crater = dynamic_cast<const terraingen::CraterLayer*>(terrain.layers[1].get());
    ASSERT_NE(crater, nullptr);
    ASSERT_TRUE(crater->Options().densityMap);
    EXPECT_EQ(crater->Options().densityMap->field->ValueAt(0.0, 0.0), 2.0);
    EXPECT_EQ(crater->Options().generation.completeness->ValueAt(0.0, 0.0), 50.0);
    const terraingen::BoulderLayerOptions options =
        terraingen::config::BuildBoulderOptions(boulders, "layers[2]");
    ASSERT_TRUE(options.densityMap);
    EXPECT_EQ(options.densityMap->field->ValueAt(0.0, 0.0), 1.5);
    ASSERT_EQ(options.manual.size(), 1u);
    EXPECT_TRUE(options.manual[0].manual);
    EXPECT_EQ(options.completeness->ValueAt(0.0, 0.0), 2.0);
    ASSERT_TRUE(options.slopeMap);
    EXPECT_EQ(options.slopeMap->field->ValueAt(0.0, 0.0), 2.5);
    EXPECT_EQ(options, options);
}

TEST(BuildTerrain, GivesCraterLayersTheSecondariesThrownFromThem) {
    TerrainConfig config = Small();
    terraingen::SecondaryLayerSpec secondaries;
    secondaries.secondaries.onsetCount = 5.0;
    config.layers.push_back(secondaries);
    config.layers.push_back(secondaries);
    config.layers.push_back(std::get<CraterLayerSpec>(config.layers.at(1)));
    const terraingen::Terrain terrain = terraingen::BuildTerrain(config);
    const auto* first = dynamic_cast<const terraingen::CraterLayer*>(terrain.layers[1].get());
    const auto* last = dynamic_cast<const terraingen::CraterLayer*>(terrain.layers[4].get());
    ASSERT_NE(first, nullptr);
    ASSERT_NE(last, nullptr);
    EXPECT_EQ(first->Options().secondaries.size(), 2u);
    EXPECT_EQ(first->Options().secondaries[0].onsetCount, 5.0);
    EXPECT_TRUE(last->Options().secondaries.empty());
}

TEST(LayerKinds, CountFindAndIndexLayersOfAKind) {
    using terraingen::BoulderLayerSpec;
    using terraingen::NoiseLayerSpec;
    using terraingen::RegionLayerSpec;
    namespace config = terraingen::config;
    const std::vector<terraingen::LayerSpec> layers{
        NoiseLayerSpec{}, CraterLayerSpec{}, BoulderLayerSpec{}, CraterLayerSpec{},
        RegionLayerSpec{terraingen::CircleRegionSpec{0.0, 0.0, 10.0}, 0.0, {}, CraterLayerSpec{}}};
    EXPECT_TRUE((config::HoldsAny<NoiseLayerSpec, BoulderLayerSpec>(layers[2])));
    EXPECT_FALSE((config::HoldsAny<NoiseLayerSpec, BoulderLayerSpec>(layers[1])));
    EXPECT_EQ(config::CountLayers<CraterLayerSpec>(layers), 2u) << "not the one in a region";
    EXPECT_EQ((config::CountLayers<CraterLayerSpec, BoulderLayerSpec>(layers)), 3u);
    EXPECT_TRUE(config::HasLayer<BoulderLayerSpec>(layers));
    EXPECT_FALSE(config::HasLayer<terraingen::DuneLayerSpec>(layers));
    EXPECT_EQ(config::LayerIndices<CraterLayerSpec>(layers), (std::vector<std::size_t>{1, 3}));
    const std::vector<terraingen::OutputSpec> outputs{terraingen::HeightmapOutput{},
                                                      terraingen::MeshOutput{}};
    EXPECT_TRUE(config::HasOutput<terraingen::MeshOutput>(outputs));
    EXPECT_FALSE(config::HasOutput<terraingen::PhotometryOutput>(outputs));
}

TEST(OutputSpacing, IsAFeatureOutputsSpacingOrZero) {
    terraingen::CraterListOutput list;
    EXPECT_EQ(terraingen::config::OutputSpacing(list), 0.0);
    list.spacing = 2.5;
    EXPECT_EQ(terraingen::config::OutputSpacing(list), 2.5);
    terraingen::BoulderModelsOutput models;
    models.spacing = 0.5;
    EXPECT_EQ(terraingen::config::OutputSpacing(models), 0.5);
    EXPECT_EQ(terraingen::config::OutputSpacing(terraingen::HeightmapOutput{}), 0.0);
    terraingen::MeshOutput mesh;
    mesh.spacing = 9.0;
    EXPECT_EQ(terraingen::config::OutputSpacing(mesh), 0.0) << "a mesh lists no features";
}

TEST(InferListFormat, FollowsTheJsonExtension) {
    EXPECT_EQ(terraingen::InferListFormat("a/craters.json"), terraingen::ListFormat::Json);
    EXPECT_EQ(terraingen::InferListFormat("craters.csv"), terraingen::ListFormat::Csv);
    EXPECT_EQ(terraingen::InferListFormat("craters"), terraingen::ListFormat::Csv);
    EXPECT_EQ(terraingen::InferListFormat("json"), terraingen::ListFormat::Csv);
}
