// SPDX-License-Identifier: BSD-3-Clause

#include "configs.h"
#include "support/mesh_measures.h"
#include "support/resample.h"
#include "support/temp_path.h"
#include "terraingen/config/build.h"
#include "terraingen/config/generate.h"
#include "terraingen/mesh/io/icq.h"
#include "terraingen/mesh/io/mesh_io.h"
#include "terraingen/mesh/polyhedra.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using terraingen::BuildCraterModels;
using terraingen::ConfigError;
using terraingen::ConfigSpec;
using terraingen::CraterLayerSpec;
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

/// A field of `value` everywhere.
terraingen::FieldSpec ConstantField(double value) {
    terraingen::FieldSpec field;
    field.value = value;
    return field;
}

/// A field read from the GeoTIFF at `path`.
terraingen::FieldSpec FileField(const std::string& path) {
    terraingen::FieldSpec field;
    field.path = path;
    return field;
}

/// A density map of a field of `value` everywhere.
terraingen::DensityMapSpec ConstantDensityMap(double value) {
    terraingen::DensityMapSpec map;
    map.field = ConstantField(value);
    return map;
}

/// A size-slope map of a field of `value` everywhere.
terraingen::SizeSlopeMapSpec ConstantSlopeMap(double value) {
    terraingen::SizeSlopeMapSpec map;
    map.field = ConstantField(value);
    return map;
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
    craters.ages = terraingen::FreshnessAgesSpec{
        0.7, {terraingen::ValueDistribution::Kind::Normal, 0.35, 0.1, -1.0, 2.0}};
    const auto drawn = BuildCraterModels(craters);
    const auto& freshness = dynamic_cast<const terraingen::FreshnessAges&>(*drawn.ages);
    EXPECT_EQ(freshness.LifetimeGyr(5.0), 0.7);
    EXPECT_EQ(freshness.LifetimeGyr(500.0), 0.7);
    EXPECT_EQ(freshness.Freshness().a, 0.35);
    EXPECT_EQ(freshness.Freshness().min, 0.0);
    EXPECT_EQ(freshness.Freshness().max, 1.0);
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

TEST(BuildCraterModels, DefaultsAsTheLibraryDoes) {
    const CraterLayerSpec spec;
    const terraingen::CraterLayerOptions options;
    EXPECT_EQ(spec.options.generation.minDiameter, options.generation.minDiameter);
    EXPECT_EQ(spec.options.generation.maxDiameter, options.generation.maxDiameter);
    EXPECT_EQ(spec.options.apply, options.apply);
    EXPECT_EQ(spec.options.seed, options.seed);
    EXPECT_EQ(spec.options.sourceResolvesPixels, options.sourceResolvesPixels);

    const terraingen::CraterLayer library(terraingen::DefaultCraterModels(), options);
    const terraingen::CraterLayer configured(BuildCraterModels(spec), options);
    const terraingen::Bounds2d region{-200.0, -200.0, 200.0, 200.0};
    const terraingen::CraterList craters = library.Craters(region, 1.0);
    ASSERT_GT(craters.Size(), 100u);
    EXPECT_EQ(craters.Ids(), configured.Craters(region, 1.0).Ids());
    EXPECT_EQ(craters.AgesGyr(), configured.Craters(region, 1.0).AgesGyr());
    for (std::size_t n = 0; n < craters.Size(); n += 7) {
        EXPECT_EQ(library.Models().Shape().Attributes(craters.At(n)),
                  configured.Models().Shape().Attributes(craters.At(n)));
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
    EXPECT_EQ(terraingen::config::BuildField(ConstantField(4.0), "f")->ValueAt(1.0, 2.0), 4.0);
    terraingen::Raster<float> raster(
        terraingen::RasterGrid{2, 2, terraingen::GeoTransform::NorthUp(0.0, 2.0, 1.0)}, 3.0f);
    const auto path = terraingen::testing::TempPath("build_field.tif");
    terraingen::WriteGeoTiff(path, raster, {{1, 1, 0, 1, 1024, 0, 1, 1}, {}, {}});
    terraingen::FieldSpec spec = FileField(path.filename().string());
    spec.read.scale = 2.0;
    spec.read.fallback = -1.0;
    const auto field = terraingen::config::BuildField(spec, "f", path.parent_path());
    EXPECT_DOUBLE_EQ(field->ValueAt(1.0, 1.0), 6.0);
    EXPECT_DOUBLE_EQ(field->ValueAt(9.0, 9.0), -1.0);
    try {
        terraingen::config::BuildField(FileField("missing.tif"), "layers[1].completeness");
        FAIL() << "expected a ConfigError";
    } catch (const ConfigError& error) {
        EXPECT_EQ(error.Path(), "layers[1].completeness");
    }
    terraingen::FieldSpec cubic = FileField(path.string());
    cubic.read.method = terraingen::ResampleMethod::Cubic;
    EXPECT_THROW(terraingen::config::BuildField(cubic, "f"), ConfigError);
}

TEST(BuildMap, BuildsTheFieldAndChecksTheMap) {
    terraingen::DensityMapSpec spec = ConstantDensityMap(5.0);
    spec.map.measure = terraingen::DensityMeasure::Count;
    spec.map.minDiameter = 4.5;
    spec.map.areaUnit = 1e6;
    spec.map.total = false;
    const terraingen::DensityMap map = terraingen::config::BuildMap(spec, "m");
    EXPECT_EQ(map.field->ValueAt(0.0, 0.0), 5.0);
    EXPECT_EQ(map.minDiameter, 4.5);
    EXPECT_TRUE(std::isinf(map.maxDiameter));
    EXPECT_EQ(map.areaUnit, 1e6);
    EXPECT_FALSE(map.total);
    terraingen::DensityMapSpec counts;
    counts.map.measure = terraingen::DensityMeasure::Count;
    try {
        terraingen::config::BuildMap(counts, "layers[0].density_map");
        FAIL() << "expected a ConfigError for a count without a smallest diameter";
    } catch (const ConfigError& error) {
        EXPECT_EQ(error.Path(), "layers[0].density_map");
    }
    try {
        terraingen::DensityMapSpec missing;
        missing.field = FileField("missing.tif");
        terraingen::config::BuildMap(missing, "layers[0].density_map");
        FAIL() << "expected a ConfigError";
    } catch (const ConfigError& error) {
        EXPECT_EQ(error.Path(), "layers[0].density_map.field");
    }
}

TEST(ValidateConfig, ReportsDensityAndSlopeMapProblemsAtTheMap) {
    ConfigSpec config = terraingen::testing::Small();
    terraingen::DensityMapSpec counts;
    counts.map.measure = terraingen::DensityMeasure::Count;
    terraingen::testing::Craters(config).densityMap = counts;
    EXPECT_EQ(terraingen::testing::ValidationErrorPath(config), "layers[1].density_map");
    config = terraingen::testing::Small();
    terraingen::BoulderLayerSpec boulders;
    boulders.sizes = terraingen::PowerLawSpec{0.01, 3.0};
    boulders.slopeMap = ConstantSlopeMap(2.5);
    boulders.slopeMap->map.maxExponent = boulders.slopeMap->map.minExponent;
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
    const terraingen::BoulderLayerOptions library;
    EXPECT_EQ(library.sizes.maxDiameter, nominal.sizes.maxDiameter) << "as the library defaults";
    for (const double d : {0.2, 1.0, 4.0}) {
        EXPECT_DOUBLE_EQ(library.sizes.distribution->Cumulative(d),
                         nominal.sizes.distribution->Cumulative(d));
        EXPECT_DOUBLE_EQ(library.ejecta.distribution->Cumulative(d),
                         nominal.ejecta.distribution->Cumulative(d));
    }
    EXPECT_EQ(library.sizes.distribution, terraingen::DefaultBoulderSizes());
    terraingen::BoulderLayerSpec overridden;
    overridden.boulders.sizes.distribution = nullptr;
    EXPECT_THROW(terraingen::config::BuildBoulderOptions(overridden, "layers[3]"), ConfigError)
        << "the sizes spec gives the distribution, which would replace this one";
    double cover = 0.0;
    constexpr int kSteps = 2000;
    const double step = std::log(nominal.sizes.maxDiameter) / kSteps;
    for (int i = 0; i < kSteps; ++i) {
        const double d = std::exp((i + 0.5) * step);
        cover +=
            0.25 * terraingen::kPi * d * d * nominal.sizes.distribution->Incremental(d) * d * step;
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

TEST(BuildRegionShape, BuildsRectanglesCirclesAndPolygons) {
    const auto rectangle =
        terraingen::config::BuildRegionShape(terraingen::RectangleRegionSpec{-1.0, -2.0, 3.0, 4.0});
    EXPECT_DOUBLE_EQ(rectangle.DistanceOutside(2.5, 3.5), -0.5);
    EXPECT_DOUBLE_EQ(rectangle.DistanceOutside(3.5, 0.0), 0.5);
    const auto circle =
        terraingen::config::BuildRegionShape(terraingen::CircleRegionSpec{10.0, 0.0, 2.0});
    EXPECT_DOUBLE_EQ(circle.DistanceOutside(11.0, 0.0), -1.0);
    EXPECT_DOUBLE_EQ(circle.DistanceOutside(12.5, 0.0), 0.5);
    const auto polygon = terraingen::config::BuildRegionShape(terraingen::PolygonRegionSpec{
        {{0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {0.0, 4.0}}, {{{1.0, 1.0}, {2.0, 1.0}, {1.0, 2.0}}}});
    EXPECT_EQ(polygon,
              terraingen::RegionShape::Polygon({{0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {0.0, 4.0}},
                                               {{{1.0, 1.0}, {2.0, 1.0}, {1.0, 2.0}}}));
    EXPECT_DOUBLE_EQ(polygon.DistanceOutside(3.0, 2.0), -1.0);
}

TEST(Resolve, ResolvesRelativePathsAgainstAFolder) {
    EXPECT_EQ(terraingen::config::Resolve("a.tif", "/data"), std::filesystem::path("/data/a.tif"));
    EXPECT_EQ(terraingen::config::Resolve("/abs/a.tif", "/data"),
              std::filesystem::path("/abs/a.tif"));
    EXPECT_EQ(terraingen::config::Resolve("a.tif", {}), std::filesystem::path("a.tif"));
}

TEST(BuildTerrain, BoulderEjectaComeFromEveryCraterLayerBefore) {
    ConfigSpec config;
    config.outputs = {
        terraingen::testing::Heightmap("e.tif", terraingen::testing::GridRegion{65, 65})};
    CraterLayerSpec first;
    first.options.generateRandom = false;
    first.ages = terraingen::FreshAgesSpec{};
    first.manual = {{0.0, 0.0, 40.0, 0.0, 1u, true}};
    CraterLayerSpec second = first;
    second.manual = {{500.0, 0.0, 40.0, 0.0, 2u, true}};
    terraingen::BoulderLayerSpec boulders;
    boulders.boulders.ejecta.minCraterDiameter = 10.0;
    config.layers = {first, second, boulders};
    const terraingen::Terrain terrain = terraingen::BuildTerrain(config);
    const auto& layer = dynamic_cast<const terraingen::BoulderLayer&>(*terrain.Layers()[2]);
    EXPECT_EQ(layer.AbundanceAt(20.0, 0.0).ejecta, 1.0) << "the first crater layer's rim";
    EXPECT_EQ(layer.AbundanceAt(520.0, 0.0).ejecta, 1.0) << "the second crater layer's rim";
}

TEST(BuildTerrain, BuildsEveryLayerKindInOrder) {
    ConfigSpec config = Small();
    terraingen::BoulderLayerSpec boulders;
    boulders.boulders.sizes.minDiameter = 1.0;
    boulders.boulders.ejecta.minCraterDiameter = 10.0;
    terraingen::SecondaryLayerSpec secondaries;
    secondaries.secondaries.onsetCount = 5.0;
    config.layers.push_back(boulders);
    config.layers.push_back(secondaries);
    config.layers.push_back(terraingen::RoughnessLayerSpec{});
    config.layers.push_back(terraingen::BedformOptions{});
    config.layers.push_back(terraingen::ScaleLayerSpec{2.0, 1.0});
    config.layers.push_back(terraingen::ClampLayerSpec{0.0, 1.0});
    config.layers.push_back(terraingen::MaskedLayerSpec{terraingen::CircleRegionSpec{0, 0, 10}});
    config.layers.push_back(terraingen::MarsBarchans());
    const terraingen::Terrain terrain = terraingen::BuildTerrain(config);
    const std::vector<terraingen::LayerPtr>& layers = terrain.Layers();
    ASSERT_EQ(layers.size(), 10u);
    EXPECT_NE(dynamic_cast<const terraingen::NoiseLayer*>(layers[0].get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::CraterLayer*>(layers[1].get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::BoulderLayer*>(layers[2].get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::SecondaryCraterLayer*>(layers[3].get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::RoughnessLayer*>(layers[4].get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::BedformLayer*>(layers[5].get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::ScaleLayer*>(layers[6].get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::ClampLayer*>(layers[7].get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::MaskedLayer*>(layers[8].get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::DuneLayer*>(layers[9].get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::SyntheticSource*>(terrain.Source().get()), nullptr);
    ConfigSpec narrow;
    narrow.outputs = {
        terraingen::testing::Heightmap("n.tif", terraingen::testing::GridRegion{1, 1})};
    EXPECT_THROW(terraingen::BuildTerrain(narrow), ConfigError);
}

TEST(BuildAlbedo, BrightensEveryOrOneCraterLayer) {
    ConfigSpec both = terraingen::testing::WithAlbedo();
    both.layers.push_back(Craters(both));
    std::get<terraingen::CraterAlbedoSpec>(both.albedo->layers[0]).craterLayer.reset();
    EXPECT_EQ(terraingen::BuildAlbedo(both, terraingen::BuildTerrain(both)).layers.size(), 2u);
    std::get<terraingen::CraterAlbedoSpec>(both.albedo->layers[0]).craterLayer = 1;
    EXPECT_EQ(terraingen::BuildAlbedo(both, terraingen::BuildTerrain(both)).layers.size(), 1u);
    both.albedo->layers.push_back(terraingen::SplotchAlbedoOptions{});
    const terraingen::Albedo spotted =
        terraingen::BuildAlbedo(both, terraingen::BuildTerrain(both));
    ASSERT_EQ(spotted.layers.size(), 2u);
    EXPECT_NE(dynamic_cast<const terraingen::SplotchAlbedo*>(spotted.layers[1].get()), nullptr);
    ConfigSpec rocky = both;
    rocky.layers.push_back(terraingen::BoulderLayerSpec{});
    rocky.layers.push_back(terraingen::BoulderLayerSpec{});
    rocky.albedo->layers = {terraingen::RockAlbedoSpec{}, terraingen::SlopeAlbedoOptions{}};
    const terraingen::Albedo rocks =
        terraingen::BuildAlbedo(rocky, terraingen::BuildTerrain(rocky));
    ASSERT_EQ(rocks.layers.size(), 2u);
    EXPECT_NE(dynamic_cast<const terraingen::RockAlbedo*>(rocks.layers[0].get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::SlopeAlbedo*>(rocks.layers[1].get()), nullptr);
    const terraingen::RasterGrid grid{8, 8, terraingen::GeoTransform::NorthUp(0.0, 8.0, 1.0)};
    EXPECT_EQ(terraingen::EvaluateAlbedo(rocks, grid, {1.0})[0].Width(), 8u)
        << "the rock's grounds outlive the terrain they were built from";
    ConfigSpec none = Small();
    EXPECT_THROW(terraingen::BuildAlbedo(none, terraingen::BuildTerrain(none)), ConfigError);
    ConfigSpec invalid = terraingen::testing::WithAlbedo();
    const terraingen::Terrain terrain = terraingen::BuildTerrain(invalid);
    std::get<terraingen::SyntheticAlbedoSpec>(invalid.albedo->source).options.value = 2.0;
    EXPECT_THROW(terraingen::BuildAlbedo(invalid, terrain), ConfigError);
}

TEST(BuildAlbedo, RejectsATerrainChangedAfterBuilding) {
    ConfigSpec rocky = terraingen::testing::WithAlbedo();
    rocky.layers.push_back(terraingen::BoulderLayerSpec{});
    rocky.albedo->layers.push_back(terraingen::RockAlbedoSpec{});
    const terraingen::Terrain built = terraingen::BuildTerrain(rocky);
    const std::size_t last = built.Layers().size() - 1;
    ASSERT_GE(last, 1u);
    const terraingen::Terrain shorter = built.Beneath(last);
    terraingen::Terrain replaced = built;
    replaced.SetLayer(last, std::make_shared<terraingen::ScaleLayer>(1.0));
    for (const terraingen::Terrain* changed :
         std::initializer_list<const terraingen::Terrain*>{&shorter, &replaced}) {
        try {
            terraingen::BuildAlbedo(rocky, *changed);
            ADD_FAILURE() << "a changed terrain was accepted";
        } catch (const ConfigError& error) {
            EXPECT_EQ(error.Path().rfind("layers[", 0), 0u) << error.Path();
        }
    }
    EXPECT_NO_THROW(terraingen::BuildAlbedo(rocky, built));
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
    const terraingen::Surface meters = terraingen::BuildSurface(
        terraingen::ShapeModelSurfaceSpec{"config_shape.icq", {}, 1.0}, ::testing::TempDir());
    EXPECT_NEAR(meters.Shape()->At({1.0, 0.0, 0.0}), 0.4, 1e-8) << "in meters, not kilometers";
}

TEST(BuildSurface, ResamplesAMeshShapeModelScaledToMeters) {
    terraingen::SceneMesh scene;
    scene.mesh = terraingen::Scaled(terraingen::testing::Icosphere(4), {0.3, 0.2, 0.1});
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
              "body.meters_per_unit");
    terraingen::SceneMesh off;
    off.mesh = terraingen::testing::Icosphere(1);
    for (terraingen::Vec3d& p : off.mesh.positions) {
        p.x += 5.0;
    }
    terraingen::WriteMesh(terraingen::testing::TempPath("config_off_center.obj"), off);
    EXPECT_EQ(SurfaceErrorPath(terraingen::ShapeModelSurfaceSpec{"config_off_center.obj", 4}),
              "body.path");
    const auto corrupt = terraingen::testing::TempPath("config_corrupt.icq");
    std::ofstream(corrupt) << "not an ICQ model\n";
    EXPECT_EQ(SurfaceErrorPath(terraingen::ShapeModelSurfaceSpec{"config_corrupt.icq"}),
              "body.path");
}

TEST(BuildTerrain, AShapeModelBodyIsItsOwnSourceUpsampledAsGiven) {
    terraingen::WriteIcq(terraingen::testing::TempPath("config_source.icq"),
                         terraingen::ToIcq(Lumpy(12)));
    ConfigSpec config;
    config.body = terraingen::ShapeModelSurfaceSpec{"config_source.icq"};
    const auto source = [&] {
        return std::dynamic_pointer_cast<const terraingen::ShapeModelSource>(
            terraingen::BuildTerrain(config, ::testing::TempDir()).Source());
    };
    ASSERT_NE(source(), nullptr) << "a shape-model body is its own source";
    const terraingen::ShapeModelSource cubic(source()->Body());
    EXPECT_EQ(source()->HeightAt(12.3, 4.5, 0.1), cubic.HeightAt(12.3, 4.5, 0.1));

    terraingen::UpsamplingSpec lanczos;
    lanczos.type = "lanczos3";
    config.source = terraingen::ShapeModelSourceSpec{lanczos};
    ASSERT_NE(source(), nullptr);
    EXPECT_NE(source()->HeightAt(12.3, 4.5, 0.1), cubic.HeightAt(12.3, 4.5, 0.1));

    config.source = terraingen::SyntheticSourceSpec{};
    EXPECT_EQ(source(), nullptr) << "a given source replaces it";

    ConfigSpec sphere;
    sphere.body = terraingen::SphereSurfaceSpec{1000.0};
    sphere.source = terraingen::ShapeModelSourceSpec{};
    try {
        terraingen::BuildTerrain(sphere);
        FAIL() << "a shape-model source needs a shape-model body";
    } catch (const ConfigError& error) {
        EXPECT_EQ(error.Path(), "source");
    }
}

TEST(GenerateFromConfig, RefinesAShapeModelBodyBetweenItsLabels) {
    terraingen::WriteIcq(terraingen::testing::TempPath("config_refined.icq"),
                         terraingen::ToIcq(Lumpy(8)));
    ConfigSpec config;
    config.body = terraingen::ShapeModelSurfaceSpec{"config_refined.icq"};
    const terraingen::Terrain terrain = terraingen::BuildTerrain(config, ::testing::TempDir());
    const terraingen::Raster<float> heights =
        terrain.Evaluate(terraingen::RasterGrid::Covering({-180.0, -90.0, 180.0, 90.0}, 3.0),
                         terraingen::EvaluationOptions{}.Spacing(1.0));
    float largest = 0.0f;
    for (const float height : heights.Values()) {
        largest = std::max(largest, std::abs(height));
    }
    EXPECT_GT(largest, 0.01f) << "cubic leaves the bilinear shape between labels";
    EXPECT_LT(largest, 5.0f);
}

TEST(BuildTerrain, GivesLayersTheirMapsCatalogsAndCompleteness) {
    ConfigSpec config = Small();
    auto& craters = std::get<CraterLayerSpec>(config.layers.at(1));
    craters.densityMap = ConstantDensityMap(2.0);
    craters.completeness = ConstantField(50.0);
    terraingen::BoulderLayerSpec boulders;
    boulders.densityMap = ConstantDensityMap(1.5);
    terraingen::ManualBoulder placed(1.0, 2.0, 3.0);
    placed.id = 7u;
    placed.burial = 0.25;
    placed.yawDegrees = 30.0;
    placed.rest = terraingen::BoulderRest::Side;
    boulders.boulders.manual = {placed};
    boulders.completeness = ConstantField(2.0);
    boulders.sizes = terraingen::PowerLawSpec{0.01, 3.0};
    boulders.slopeMap = ConstantSlopeMap(2.5);
    config.layers.push_back(boulders);
    const terraingen::Terrain terrain = terraingen::BuildTerrain(config);
    const auto* crater = dynamic_cast<const terraingen::CraterLayer*>(terrain.Layers()[1].get());
    ASSERT_NE(crater, nullptr);
    ASSERT_TRUE(crater->Options().densityMap);
    EXPECT_EQ(crater->Options().densityMap->field->ValueAt(0.0, 0.0), 2.0);
    EXPECT_EQ(crater->Options().generation.completeness->ValueAt(0.0, 0.0), 50.0);
    const terraingen::BoulderLayerOptions options =
        terraingen::config::BuildBoulderOptions(boulders, "layers[2]");
    ASSERT_TRUE(options.densityMap);
    EXPECT_EQ(options.densityMap->field->ValueAt(0.0, 0.0), 1.5);
    ASSERT_EQ(options.manual.size(), 1u);
    EXPECT_EQ(options.manual[0], placed) << "every fixed field handed on";
    EXPECT_EQ(options.completeness->ValueAt(0.0, 0.0), 2.0);
    ASSERT_TRUE(options.slopeMap);
    EXPECT_EQ(options.slopeMap->field->ValueAt(0.0, 0.0), 2.5);
    EXPECT_EQ(options, options);
}

TEST(BuildTerrain, GivesCraterLayersTheSecondariesThrownFromThem) {
    ConfigSpec config = Small();
    terraingen::SecondaryLayerSpec secondaries;
    secondaries.secondaries.onsetCount = 5.0;
    config.layers.push_back(secondaries);
    config.layers.push_back(secondaries);
    config.layers.push_back(std::get<CraterLayerSpec>(config.layers.at(1)));
    const terraingen::Terrain terrain = terraingen::BuildTerrain(config);
    const auto* first = dynamic_cast<const terraingen::CraterLayer*>(terrain.Layers()[1].get());
    const auto* last = dynamic_cast<const terraingen::CraterLayer*>(terrain.Layers()[4].get());
    ASSERT_NE(first, nullptr);
    ASSERT_NE(last, nullptr);
    EXPECT_EQ(first->Options().secondaries.size(), 2u);
    EXPECT_EQ(first->Options().secondaries[0].onsetCount, 5.0);
    EXPECT_TRUE(last->Options().secondaries.empty());
}

TEST(LayerKinds, CountFindAndIndexLayersOfAKind) {
    using terraingen::BoulderLayerSpec;
    using terraingen::MaskedLayerSpec;
    using terraingen::NoiseLayerSpec;
    namespace config = terraingen::config;
    const std::vector<terraingen::LayerSpec> layers{
        NoiseLayerSpec{}, CraterLayerSpec{}, BoulderLayerSpec{}, CraterLayerSpec{},
        MaskedLayerSpec{terraingen::CircleRegionSpec{0.0, 0.0, 10.0}, 0.0, {}, CraterLayerSpec{}}};
    EXPECT_TRUE((config::HoldsAny<NoiseLayerSpec, BoulderLayerSpec>(layers[2])));
    EXPECT_FALSE((config::HoldsAny<NoiseLayerSpec, BoulderLayerSpec>(layers[1])));
    EXPECT_EQ(config::CountLayers<CraterLayerSpec>(layers), 2u) << "not the one in a region";
    EXPECT_EQ((config::CountLayers<CraterLayerSpec, BoulderLayerSpec>(layers)), 3u);
    EXPECT_EQ(config::LayerIndices<CraterLayerSpec>(layers), (std::vector<std::size_t>{1, 3}));
    const std::vector<terraingen::OutputSpec> outputs{terraingen::HeightmapOutput{},
                                                      terraingen::MeshOutput{}};
    EXPECT_TRUE(config::HasOutput<terraingen::MeshOutput>(outputs));
    EXPECT_FALSE(config::HasOutput<terraingen::PhotometryOutput>(outputs));
}

TEST(CheckOutputs, GivesTheFeatureSpacingOfAWholeBodyListOrZero) {
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    const std::optional<terraingen::RasterGrid> none;
    const terraingen::RasterGrid base{4, 4, terraingen::GeoTransform::NorthUp(0.0, 4.0, 1.0)};
    const auto spacing = [](const terraingen::OutputSpec& output,
                            const std::optional<terraingen::RasterGrid>& source,
                            const terraingen::Surface& surface) {
        return terraingen::config::CheckOutputs({output}, {}, surface, source)
            .front()
            .featureSpacing;
    };
    terraingen::CraterListOutput list;
    list.area.pixelSize = 2500.0;
    EXPECT_EQ(spacing(list, none, moon), 2500.0) << "the whole body at its pixel size";
    EXPECT_THROW(spacing(list, none, {}), ConfigError) << "a plane has no whole body";
    EXPECT_EQ(spacing(list, base, moon), 0.0) << "the source's grid at its own spacing";
    list.area.extentDeg = terraingen::ExtentSpec{{{0.0, 0.0}}, {{1.0, 1.0}}, {}, {}};
    EXPECT_EQ(spacing(list, none, moon), 0.0) << "an extent's grid at its own spacing";
    terraingen::BoulderModelsOutput models;
    models.area.pixelSize = 500.0;
    EXPECT_EQ(spacing(models, none, moon), 500.0);
    terraingen::HeightmapOutput heightmap;
    heightmap.area.pixelSize = 500.0;
    EXPECT_EQ(spacing(heightmap, base, moon), 0.0) << "a heightmap lists no features";
    terraingen::MeshOutput mesh;
    mesh.spacing = 9.0;
    EXPECT_EQ(spacing(mesh, none, moon), 0.0) << "a mesh lists no features";
}

TEST(InferListFormat, FollowsTheJsonExtension) {
    EXPECT_EQ(terraingen::InferListFormat("a/craters.json"), terraingen::ListFormat::Json);
    EXPECT_EQ(terraingen::InferListFormat("craters.csv"), terraingen::ListFormat::Csv);
    EXPECT_EQ(terraingen::InferListFormat("craters"), terraingen::ListFormat::Csv);
    EXPECT_EQ(terraingen::InferListFormat("json"), terraingen::ListFormat::Csv);
    EXPECT_EQ(terraingen::InferListFormat("a/CRATERS.JSON"), terraingen::ListFormat::Json);
    EXPECT_EQ(terraingen::InferListFormat("craters.Json"), terraingen::ListFormat::Json);
}

namespace {

/// The craters of the crater layer `index` of `terrain` centered in `region`.
terraingen::CraterList CratersIn(const terraingen::Terrain& terrain, std::size_t index,
                                 const terraingen::Bounds2d& region) {
    return dynamic_cast<const terraingen::CraterLayer&>(*terrain.Layers().at(index))
        .Craters(region);
}

/// The completeness the crater layer `index` of `terrain` was built with.
std::shared_ptr<const terraingen::SurfaceField>
CraterCompleteness(const terraingen::Terrain& terrain, std::size_t index) {
    return dynamic_cast<const terraingen::CraterLayer&>(*terrain.Layers().at(index))
        .Options()
        .generation.completeness;
}

/// Writes a flat 64 x 64 DEM of 5 m cells over (0, 0) to (320, 320) as `name` in `folder`, and
/// returns a configuration with it as the base and one crater layer of 6 to 60 m craters.
ConfigSpec FlatDemCraters(const std::filesystem::path& folder, const std::string& name) {
    terraingen::Raster<float> dem(
        terraingen::RasterGrid{64, 64, terraingen::GeoTransform::NorthUp(0.0, 320.0, 5.0)}, 1.0f);
    std::filesystem::create_directories(folder);
    terraingen::WriteGeoTiff(folder / name, dem, terraingen::testing::Keys());
    ConfigSpec config = terraingen::testing::RasterConfig(name);
    CraterLayerSpec craters = std::get<CraterLayerSpec>(Small().layers.at(1));
    craters.options.generation.minDiameter = 6.0;
    config.layers = {craters};
    return config;
}

/// The path of the `ConfigError` that building `config` from `folder` throws.
std::string BuildErrorPath(const ConfigSpec& config, const std::filesystem::path& folder) {
    try {
        terraingen::BuildTerrain(config, folder);
    } catch (const ConfigError& error) {
        return error.Path();
    }
    return "built";
}

}  // namespace

TEST(BuildTerrain, ReadsOnlyTheCropOfARasterSource) {
    const auto folder = terraingen::testing::TempPath("terraingen_config_region");
    terraingen::testing::WriteDem(folder, "dem.tif");
    ConfigSpec config = terraingen::testing::RasterConfig("dem.tif");
    const terraingen::Bounds2d region{1050.0, 1900.0, 1100.0, 1950.0};
    terraingen::testing::RasterSpec(config).read.crop = region;
    const terraingen::Terrain terrain = terraingen::BuildTerrain(config, folder);
    const terraingen::RasterGrid grid = terrain.Source()->Grid();
    terraingen::GeoTiffSourceOptions read;
    read.crop = region;
    EXPECT_EQ(grid, terraingen::GeoTiffSource(folder / "dem.tif", read).Grid());
    EXPECT_LT(grid.width, 40u);
    EXPECT_LT(grid.height, 30u);
    const terraingen::Bounds2d bounds = grid.Bounds();
    EXPECT_LE(bounds.minX, region.minX);
    EXPECT_GE(bounds.minX, region.minX - 5.0);
    EXPECT_GE(bounds.maxX, region.maxX);
    EXPECT_LE(bounds.maxX, region.maxX + 5.0);
    EXPECT_TRUE(std::isnan(terrain.Source()->HeightAt(1010.0, 1990.0, 1.0))) << "outside the crop";
}

TEST(BuildTerrain, ReadsALongitudeAndLatitudeRasterOnABody) {
    const auto folder = terraingen::testing::TempPath("terraingen_config_lonlat");
    std::filesystem::create_directories(folder);
    terraingen::Raster<float> dem(
        terraingen::RasterGrid{8, 4, terraingen::GeoTransform::NorthUp(-180.0, 90.0, 45.0)}, 0.0f);
    dem(3, 1) = 250.0f;
    terraingen::WriteGeoTiff(folder / "moon.tif", dem, terraingen::GeographicSphereKeys(1737400.0));
    ConfigSpec config = terraingen::testing::RasterConfig("moon.tif");
    config.body = terraingen::SphereSurfaceSpec{1737400.0};
    const terraingen::Terrain terrain = terraingen::BuildTerrain(config, folder);
    EXPECT_TRUE(terrain.Surface().IsBody());
    EXPECT_TRUE(terrain.Keys().IsGeographic());
    const terraingen::Raster<float> heights = terrain.Evaluate();
    EXPECT_EQ(heights.Width(), 8u);
    EXPECT_EQ(heights(3, 1), 250.0f);
}

TEST(BuildTerrain, UpsamplesTheSourceAsItsUpsamplingSays) {
    const auto folder = terraingen::testing::TempPath("terraingen_config_upsampling");
    const terraingen::Raster<float> dem = terraingen::testing::WriteDem(folder, "dem.tif");
    ConfigSpec config = terraingen::testing::RasterConfig("dem.tif");
    const terraingen::RasterGrid fine = terraingen::testing::ResampledGrid(dem.Grid(), 1.0);
    const terraingen::Raster<float> cubic = terraingen::BuildTerrain(config, folder).Evaluate(fine);
    const terraingen::Raster<float> expected =
        terraingen::testing::Resample(dem, 1.0, terraingen::ResampleMethod::Cubic);
    for (std::size_t i = 0; i < cubic.Size(); ++i) {
        ASSERT_EQ(cubic.Values()[i], expected.Values()[i]) << "cubic by default";
    }
    terraingen::UpsamplingSpec& upsampling = terraingen::testing::RasterSpec(config).upsampling;
    upsampling.type = "diamond_square";
    upsampling.diamondSquare.minPixelSize = 2.0;
    const terraingen::Terrain terrain = terraingen::BuildTerrain(config, folder);
    const auto source =
        std::dynamic_pointer_cast<const terraingen::GeoTiffSource>(terrain.Source());
    ASSERT_NE(source, nullptr);
    const terraingen::Raster<float> refined = terrain.Evaluate(fine);
    EXPECT_NE(refined(8, 6), cubic(8, 6));
    EXPECT_EQ(refined(2, 2), dem(0, 0)) << "the cells stay";
    upsampling.type = "lanczos3";
    EXPECT_NE(terraingen::BuildTerrain(config, folder).Evaluate(fine)(8, 6), cubic(8, 6));
}

TEST(BuildTerrain, BuildsASyntheticSourceOrADefaultFbmOneWithoutASource) {
    ConfigSpec config = Small();
    config.source.reset();
    const terraingen::Terrain rolling = terraingen::BuildTerrain(config);
    const auto base =
        std::dynamic_pointer_cast<const terraingen::SyntheticSource>(rolling.Source());
    ASSERT_NE(base, nullptr);
    ASSERT_EQ(base->Fractals().size(), 1u);
    const terraingen::Fractal& fractal = base->Fractals()[0];
    EXPECT_EQ(fractal.seed, 2u);
    EXPECT_EQ(fractal.wavelength, 400.0f);
    EXPECT_EQ(fractal.amplitude, 12.0f);
    EXPECT_EQ(fractal.type, terraingen::FractalType::Fbm);
    EXPECT_NE(base->HeightAt(123.0, -4.0, 1.0), 0.0f);
    EXPECT_LE(std::fabs(base->HeightAt(123.0, -4.0, 1.0)), 12.0f);
    config.source = terraingen::SyntheticSourceSpec{};
    const terraingen::RasterGrid grid =
        terraingen::RasterGrid::Centered({0.0, 0.0}, {64.0, 64.0}, 1.0);
    const terraingen::Raster<float> given = terraingen::BuildTerrain(config).Evaluate(grid);
    const terraingen::Raster<float> unset = rolling.Evaluate(grid);
    EXPECT_TRUE(std::equal(given.Values().begin(), given.Values().end(), unset.Values().begin()))
        << "a synthetic source without a stack is the same default";
    std::get<terraingen::SyntheticSourceSpec>(*config.source).stack.clear();
    const terraingen::Terrain flat = terraingen::BuildTerrain(config);
    const auto datum = std::dynamic_pointer_cast<const terraingen::SyntheticSource>(flat.Source());
    ASSERT_NE(datum, nullptr);
    EXPECT_TRUE(datum->Fractals().empty()) << "an empty stack is flat";
    EXPECT_EQ(datum->HeightAt(123.0, -4.0, 1.0), 0.0f);

    terraingen::SyntheticSourceSpec synthetic;
    synthetic.elevation = -20.0;
    synthetic.stack = {terraingen::FractalSpec{}};
    synthetic.pixelSize = 16.0;
    terraingen::UpsamplingSpec bilinear;
    bilinear.type = "bilinear";
    synthetic.upsampling = bilinear;
    config.source = synthetic;
    const terraingen::Terrain noise = terraingen::BuildTerrain(config);
    const auto made = std::dynamic_pointer_cast<const terraingen::SyntheticSource>(noise.Source());
    ASSERT_NE(made, nullptr);
    const terraingen::SyntheticSource bilinearPosts(
        terraingen::StackFractals(synthetic.stack), -20.0f, 16.0,
        std::make_shared<terraingen::KernelUpsampler>(terraingen::ResampleMethod::Bilinear));
    EXPECT_EQ(made->HeightAt(5.0, 3.0, 1.0), bilinearPosts.HeightAt(5.0, 3.0, 1.0));
    EXPECT_EQ(made->PixelSize(), 16.0);
    EXPECT_EQ(made->Fractals(), terraingen::StackFractals(synthetic.stack));
    const auto kernel =
        std::dynamic_pointer_cast<const terraingen::KernelUpsampler>(made->UpsamplerInUse());
    ASSERT_NE(kernel, nullptr);
}

TEST(BuildTerrain, MakesRegisteredUpsamplersWithTheirSettings) {
    std::string received;
    terraingen::RegisterUpsampler("config_test_kernel", [&received](const std::string& settings) {
        received = settings;
        return std::make_shared<terraingen::KernelUpsampler>(terraingen::ResampleMethod::Nearest);
    });
    const auto folder = terraingen::testing::TempPath("terraingen_config_registered");
    terraingen::testing::WriteDem(folder, "dem.tif");
    ConfigSpec config = terraingen::testing::RasterConfig("dem.tif");
    terraingen::testing::RasterSpec(config).upsampling.type = "config_test_kernel";
    terraingen::testing::RasterSpec(config).upsampling.settings = R"({"sharpness":2})";
    terraingen::BuildTerrain(config, folder);
    EXPECT_EQ(received, R"({"sharpness":2})");
    terraingen::testing::RasterSpec(config).upsampling.settings.clear();
    terraingen::BuildTerrain(config, folder);
    EXPECT_EQ(received, "{}");
}

TEST(BuildTerrain, ReportsACropThatIsInvertedNotFiniteOrOffTheFile) {
    const auto folder = terraingen::testing::TempPath("terraingen_config_bad_region");
    terraingen::testing::WriteDem(folder, "dem.tif");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (const terraingen::Bounds2d& region :
         {terraingen::Bounds2d{1100.0, 1900.0, 1050.0, 1950.0},
          terraingen::Bounds2d{1050.0, nan, 1100.0, 1950.0},
          terraingen::Bounds2d{5000.0, 5000.0, 6000.0, 6000.0}}) {
        ConfigSpec cropped = terraingen::testing::RasterConfig("dem.tif");
        terraingen::testing::RasterSpec(cropped).read.crop = region;
        EXPECT_EQ(BuildErrorPath(cropped, folder).rfind("source", 0), 0u) << region.minX;
    }
}

TEST(BuildTerrain, GeneratesOnlyTheCratersTheDemDoesNotResolveByDefault) {
    const auto folder = terraingen::testing::TempPath("terraingen_config_unresolved");
    const ConfigSpec limited = FlatDemCraters(folder, "flat.tif");
    ASSERT_EQ(std::get<CraterLayerSpec>(limited.layers.at(0)).options.sourceResolvesPixels, 4.0);
    ConfigSpec every = limited;
    std::get<CraterLayerSpec>(every.layers.at(0)).options.sourceResolvesPixels.reset();
    const terraingen::Bounds2d region{20.0, 20.0, 300.0, 300.0};
    const terraingen::CraterList all =
        CratersIn(terraingen::BuildTerrain(every, folder), 0, region);
    const terraingen::CraterList kept =
        CratersIn(terraingen::BuildTerrain(limited, folder), 0, region);
    std::vector<std::uint64_t> unresolved;
    std::size_t resolved = 0;
    std::size_t offDemLarge = 0;
    for (std::size_t n = 0; n < all.Size(); ++n) {
        const terraingen::Crater crater = all.At(n);
        const bool onDem =
            crater.x >= 0.0 && crater.x < 320.0 && crater.y >= 0.0 && crater.y < 320.0;
        if (crater.diameter >= 20.0 && onDem) {
            ++resolved;
        } else {
            unresolved.push_back(crater.id);
            offDemLarge += crater.diameter >= 20.0 ? 1 : 0;
        }
    }
    ASSERT_GT(resolved, 0u);
    ASSERT_GT(unresolved.size(), offDemLarge) << "craters below 20 m are kept";
    EXPECT_GT(offDemLarge, 0u) << "large craters off the DEM are kept";
    ASSERT_EQ(kept.Size(), unresolved.size());
    for (std::size_t n = 0; n < kept.Size(); ++n) {
        EXPECT_EQ(kept.At(n).id, unresolved[n]) << n;
    }
}

TEST(BuildTerrain, TakesTheLesserOfTheCatalogAndUnresolvedLimits) {
    const auto folder = terraingen::testing::TempPath("terraingen_config_unresolved_catalog");
    ConfigSpec config = FlatDemCraters(folder, "flat.tif");
    auto& craters = std::get<CraterLayerSpec>(config.layers.at(0));
    craters.completeness = ConstantField(50.0);
    const auto completeness = [&](const ConfigSpec& built) {
        return CraterCompleteness(terraingen::BuildTerrain(built, folder), 0);
    };
    const auto wide = completeness(config);
    ASSERT_NE(wide, nullptr);
    EXPECT_EQ(wide->ValueAt(100.0, 100.0), 20.0) << "4 cells of 5 m";
    EXPECT_EQ(wide->ValueAt(-500.0, -500.0), 50.0) << "off the DEM, the catalog's";
    craters.completeness = ConstantField(12.0);
    EXPECT_EQ(completeness(config)->ValueAt(100.0, 100.0), 12.0);
    const terraingen::Bounds2d region{20.0, 20.0, 300.0, 300.0};
    const terraingen::CraterList kept =
        CratersIn(terraingen::BuildTerrain(config, folder), 0, region);
    ASSERT_GT(kept.Size(), 0u);
    for (std::size_t n = 0; n < kept.Size(); ++n) {
        EXPECT_LT(kept.At(n).diameter, 12.0) << n;
    }
}

TEST(BuildTerrain, IgnoresTheSourceResolvedLimitOnASyntheticSource) {
    ConfigSpec config = Small();
    Craters(config).options.sourceResolvesPixels = 4.0;
    const auto completeness = [](const ConfigSpec& built) {
        return CraterCompleteness(terraingen::BuildTerrain(built), 1);
    };
    EXPECT_EQ(completeness(config), nullptr);
    terraingen::SyntheticSourceSpec posts;
    posts.pixelSize = 5.0;
    config.source = posts;
    EXPECT_EQ(completeness(config), nullptr) << "synthetic posts are not measured data";
    Craters(config).completeness = ConstantField(30.0);
    ASSERT_NE(completeness(config), nullptr);
    EXPECT_EQ(completeness(config)->ValueAt(0.0, 0.0), 30.0);
}

TEST(BuildTerrain, GivesEachRasterOfAChainItsOwnSourceResolvedLimit) {
    const auto folder = terraingen::testing::TempPath("terraingen_config_resolved_chain");
    const ConfigSpec fine = FlatDemCraters(folder, "fine.tif");
    terraingen::Raster<float> coarse(
        terraingen::RasterGrid{20, 20, terraingen::GeoTransform::NorthUp(-500.0, 500.0, 50.0)},
        1.0f);
    terraingen::WriteGeoTiff(folder / "coarse.tif", coarse, terraingen::testing::Keys());
    terraingen::SyntheticSourceSpec synthetic;
    synthetic.pixelSize = 1.0;
    ConfigSpec config = fine;
    config.source = terraingen::ChainSourceSpec{
        {*fine.source, terraingen::RasterSourceSpec{"coarse.tif"}, synthetic}};
    const auto limit = CraterCompleteness(terraingen::BuildTerrain(config, folder), 0);
    ASSERT_NE(limit, nullptr);
    EXPECT_EQ(limit->ValueAt(100.0, 100.0), 20.0) << "4 cells of 5 m";
    EXPECT_EQ(limit->ValueAt(-300.0, -300.0), 200.0) << "4 cells of 50 m";
    EXPECT_EQ(limit->ValueAt(900.0, 900.0), std::numeric_limits<double>::infinity())
        << "only the synthetic source there";
    config.source = terraingen::ChainSourceSpec{{synthetic}};
    config.outputs.clear();
    EXPECT_EQ(CraterCompleteness(terraingen::BuildTerrain(config, folder), 0), nullptr);
}

TEST(BuildTerrain, LeavesOutWhatTheRasterResolvesInEveryLayerKind) {
    const auto folder = terraingen::testing::TempPath("terraingen_config_resolved_layers");
    const ConfigSpec base = FlatDemCraters(folder, "flat.tif");
    ConfigSpec config = base;
    terraingen::DuneOptions dunes;
    dunes.shape = terraingen::DuneShape::Transverse;
    dunes.wavelength = 25.0;
    dunes.height = 1.0;
    terraingen::BedformOptions bedforms;
    bedforms.wavelength = 25.0;
    bedforms.cover = 0.9;
    terraingen::NoiseLayerSpec noise;
    noise.stack[0].fractal.wavelength = 40.0f;
    noise.sourceResolvesPixels = 4.0;
    config.layers = {dunes, bedforms, noise};
    ConfigSpec unset = config;
    std::get<terraingen::DuneOptions>(unset.layers[0]).sourceResolvesPixels.reset();
    std::get<terraingen::BedformOptions>(unset.layers[1]).sourceResolvesPixels.reset();
    std::get<terraingen::NoiseLayerSpec>(unset.layers[2]).sourceResolvesPixels.reset();
    const terraingen::Terrain limited = terraingen::BuildTerrain(config, folder);
    const terraingen::Terrain every = terraingen::BuildTerrain(unset, folder);
    terraingen::LayerContext context;
    context.spacing = 0.5;
    context.surface = &limited.Surface();
    context.source = limited.Source().get();
    for (std::size_t i = 0; i < 3; ++i) {
        bool differs = false;
        for (double x = 1.0; x < 300.0; x += 3.7) {
            const float held = limited.Layers()[i]->ApplyAt(0.0f, x, 150.0, context);
            EXPECT_EQ(held, 0.0f) << i << " at " << x << ": 25 m and 40 m are over 20 m";
            differs = differs || every.Layers()[i]->ApplyAt(0.0f, x, 150.0, context) != held;
        }
        EXPECT_TRUE(differs) << i;
    }
}

TEST(BuildTerrain, ReportsASourceResolvedLimitThatIsNotPositiveAndFinite) {
    for (const double pixels : {0.0, -2.0, std::numeric_limits<double>::infinity()}) {
        ConfigSpec config = Small();
        Craters(config).options.sourceResolvesPixels = pixels;
        EXPECT_EQ(BuildErrorPath(config, {}), "layers[1].source_resolves_px") << pixels;
    }
}

namespace {

/// A 200 x 200 raster source of 20 m cells at a constant 100 m, centered on the origin.
std::shared_ptr<const terraingen::Source> FlatDem() {
    terraingen::Raster<float> heights(
        terraingen::RasterGrid{200, 200, terraingen::GeoTransform::NorthUp(-2000.0, 2000.0, 20.0)},
        100.0f);
    return std::make_shared<terraingen::RasterSource>(std::move(heights));
}

}  // namespace

TEST(BuildTerrain, BuildsOnAGivenSourceAsMeasuredData) {
    const ConfigSpec config = terraingen::ParseConfig(R"({
        "schema_version": 1,
        "layers": [
            {"type": "craters", "generate_random": false,
             "manual": [{"x": 0, "y": 0, "diameter_m": 50, "depth_m": 10}]}
        ]
    })");
    const auto dem = FlatDem();
    const terraingen::Terrain onDem = terraingen::BuildTerrain(config, dem);
    ConfigSpec onFlat = config;
    terraingen::SyntheticSourceSpec flat;
    flat.stack.clear();
    onFlat.source = flat;
    const terraingen::Terrain alone = terraingen::BuildTerrain(onFlat);

    EXPECT_EQ(onDem.Source(), dem);
    EXPECT_NEAR(onDem.HeightAt(300.0, 300.0, 1.0), 100.0, 1e-4);
    EXPECT_NEAR(onDem.HeightAt(0.0, 0.0, 1.0), 100.0 + alone.HeightAt(0.0, 0.0, 1.0), 1e-3)
        << "the crater is cut into the given source";
}

TEST(BuildTerrain, LeavesOutCratersTheGivenSourceResolves) {
    const ConfigSpec config = terraingen::ParseConfig(R"({
        "schema_version": 1,
        "layers": [{"type": "craters", "seed": 2, "min_diameter_m": 20, "max_diameter_m": 1000}]
    })");
    const terraingen::RasterGrid grid =
        terraingen::RasterGrid::Centered({0.0, 0.0}, {4000.0, 4000.0}, 20.0);
    const auto largest = [&](const terraingen::Terrain& terrain) {
        const terraingen::CraterList craters = terrain.Craters(grid).craters;
        double most = 0.0;
        for (std::size_t i = 0; i < craters.Size(); ++i) {
            if (std::abs(craters.X()[i]) < 1500.0 && std::abs(craters.Y()[i]) < 1500.0) {
                most = std::max(most, craters.Diameters()[i]);
            }
        }
        return most;
    };
    EXPECT_GT(largest(terraingen::BuildTerrain(config)), 80.0);
    EXPECT_LT(largest(terraingen::BuildTerrain(config, FlatDem())), 80.0)
        << "craters 4 cells (80 m) across and up are in the 20 m source where it has data";
}

TEST(BuildTerrain, AGivenSourceReplacesNoneOfTheConfigurations) {
    const ConfigSpec withSource = terraingen::ParseConfig(R"({
        "schema_version": 1,
        "source": {"type": "synthetic"},
        "layers": []
    })");
    EXPECT_THROW(terraingen::BuildTerrain(withSource, FlatDem()), ConfigError);
    EXPECT_THROW(terraingen::BuildTerrain(Small(), nullptr), std::invalid_argument);
}

TEST(BuildTerrain, GivesAChainItsSeamsInTheSurfacesUnits) {
    const auto folder = terraingen::testing::TempPath("terraingen_config_chain_seams");
    std::filesystem::create_directories(folder);
    terraingen::Raster<float> dem(
        terraingen::RasterGrid{8, 4, terraingen::GeoTransform::NorthUp(-4.0, 2.0, 1.0)}, 1.0f);
    terraingen::WriteGeoTiff(folder / "degrees.tif", dem, terraingen::testing::Keys(true));
    terraingen::ChainSourceSpec chain{
        {terraingen::RasterSourceSpec{"degrees.tif"}, terraingen::SyntheticSourceSpec{}}};
    chain.seams.mode = terraingen::ChainSeamMode::Slope;
    chain.seams.width = 120.0;
    chain.seams.decayLength = 300.0;
    ConfigSpec config;
    config.body = terraingen::SphereSurfaceSpec{1737400.0};
    config.source = chain;
    const terraingen::Terrain terrain = terraingen::BuildTerrain(config, folder);
    const auto* built = dynamic_cast<const terraingen::SourceChain*>(terrain.Source().get());
    ASSERT_NE(built, nullptr);
    const double unit = terraingen::Surface::Sphere(1737400.0).UnitLength();
    EXPECT_EQ(built->Seams().mode, terraingen::ChainSeamMode::Slope);
    EXPECT_DOUBLE_EQ(built->Seams().width, 120.0 / unit) << "degrees along a meridian";
    EXPECT_DOUBLE_EQ(built->Seams().decayLength, 300.0 / unit);
    config.body = terraingen::PlaneSurfaceSpec{};
    chain.sources.front() = terraingen::RasterSourceSpec{"meters.tif"};
    terraingen::WriteGeoTiff(folder / "meters.tif", dem, terraingen::testing::Keys());
    config.source = chain;
    const terraingen::Terrain plane = terraingen::BuildTerrain(config, folder);
    EXPECT_EQ(dynamic_cast<const terraingen::SourceChain&>(*plane.Source()).Seams(), chain.seams)
        << "meters on a plane";
}

TEST(BuildTerrain, ReplacesTheInheritedCratersTheSourceResolves) {
    const auto folder = terraingen::testing::TempDirectory("terraingen_inherited");
    std::ofstream(folder / "coarse.csv") << "x,y,diameter,age_gyr,id,depth_ratio,rim_height\n"
                                            "0,0,100,0.5,21,0.15,2\n"
                                            "300,300,50,0.5,22,0.1,1\n";
    const ConfigSpec config = terraingen::ParseConfig(R"({
        "schema_version": 1,
        "layers": [
            {"type": "craters", "generate_random": false,
             "manual": [{"x": 900, "y": 0, "diameter_m": 60, "replace": true}],
             "replace_inherited_craters": {"path": "coarse.csv"}}
        ]
    })");
    const terraingen::Terrain terrain = terraingen::BuildTerrain(config, FlatDem(), folder);
    const auto& layer = dynamic_cast<const terraingen::CraterLayer&>(*terrain.Layers()[0]);
    const terraingen::CraterList& manual = layer.Options().generation.manual;
    ASSERT_EQ(manual.Size(), 2u) << "50 m is under 4 cells of 20 m";
    EXPECT_TRUE(manual.At(0).replace);
    EXPECT_EQ(manual.At(1).id, 21u);
    EXPECT_TRUE(manual.At(1).replace);
    EXPECT_DOUBLE_EQ(*manual.At(1).depth, 15.0);
    EXPECT_FALSE(manual.At(1).rimHeight) << "the diffused profile ties the rim to the depth";
    EXPECT_LT(terrain.HeightAt(0.0, 0.0, 1.0), 100.0 - 10.0) << "cut sharp into the flat DEM";
}

TEST(BuildTerrain, ReportsAnUnreadableInheritedListAtItsPath) {
    ConfigSpec config;
    config.outputs = {
        terraingen::testing::Heightmap("e.tif", terraingen::testing::GridRegion{65, 65})};
    CraterLayerSpec craters;
    craters.replaceInheritedCraters = terraingen::InheritedCratersSpec{"missing.csv"};
    config.layers = {craters};
    try {
        terraingen::BuildTerrain(config, FlatDem(), "/nonexistent");
        ADD_FAILURE() << "read a missing crater list";
    } catch (const ConfigError& error) {
        EXPECT_EQ(error.Path(), "layers[0].replace_inherited_craters.path");
    }
}
