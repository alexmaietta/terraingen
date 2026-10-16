#include "terraingen/layers/boulders/boulder_layer.h"

#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>

using terraingen::Boulder;
using terraingen::BoulderLayer;
using terraingen::BoulderLayerOptions;
using terraingen::Bounds2d;
using terraingen::GeoTransform;
using terraingen::Raster;

namespace {

/// Craters with no generation, only `manual` ones, fresh.
std::shared_ptr<const terraingen::CraterLayer>
Craters(const std::vector<terraingen::Crater>& manual) {
    terraingen::CraterLayerOptions options;
    options.generate = false;
    for (const auto& crater : manual) {
        options.generation.manual.Append(crater);
    }
    return std::make_shared<terraingen::CraterLayer>(
        terraingen::CraterModels{std::make_shared<terraingen::PowerLawSfd>(0.05, 2.0),
                                 std::make_shared<terraingen::FreshAges>(),
                                 std::make_shared<terraingen::FreshnessDegradation>(
                                     std::make_shared<terraingen::UniformAges>(1.0),
                                     terraingen::LunarFreshCraterMorphometry),
                                 std::make_shared<terraingen::FassettThomsonProfile>()},
        options);
}

class Analytic final : public terraingen::Ground {
  public:
    explicit Analytic(std::function<float(double, double)> height) : height_(std::move(height)) {}
    float HeightAt(double x, double y, double /*spacing*/) const override {
        return height_(x, y);
    }

  private:
    std::function<float(double, double)> height_;
};

}  // namespace

TEST(BoulderLayer, GivesEachBoulderStableAttributesInTheirRanges) {
    BoulderLayerOptions options;
    options.sizes.coverage = 0.05;
    const BoulderLayer layer(options);
    const auto boulders = layer.Boulders({-20.0, -20.0, 20.0, 20.0});
    ASSERT_GT(boulders.size(), 50u);
    std::set<std::uint64_t> ids;
    for (const Boulder& boulder : boulders) {
        EXPECT_GE(boulder.diameter, 0.1);
        EXPECT_LT(boulder.diameter, 4.0);
        EXPECT_GE(boulder.height / boulder.diameter, 0.4 - 1e-12);
        EXPECT_LE(boulder.height / boulder.diameter, 0.6 + 1e-12);
        EXPECT_GE(boulder.burial, 0.1);
        EXPECT_LE(boulder.burial, 0.4);
        EXPECT_GE(boulder.yaw, 0.0);
        EXPECT_LT(boulder.yaw, 2.0 * std::numbers::pi);
        EXPECT_LE(boulder.lean, 0.26);
        EXPECT_DOUBLE_EQ(boulder.ExposedHeight(), boulder.height * (1.0 - boulder.burial));
        ids.insert(boulder.id);
    }
    EXPECT_EQ(ids.size(), boulders.size());
    EXPECT_EQ(layer.Boulders({-20.0, -20.0, 20.0, 20.0}), boulders);
}

TEST(BoulderLayer, GivesTheSameBouldersInEveryRegionAndSkipsOnlyUnresolvedOnes) {
    BoulderLayerOptions options;
    options.sizes.coverage = 0.05;
    options.patches = {2.0, 30.0};
    const BoulderLayer layer(options);
    const auto whole = layer.Boulders({-30.0, -30.0, 30.0, 30.0});
    const auto part = layer.Boulders({-5.0, 0.0, 10.0, 12.0});
    std::set<std::uint64_t> wholeIds;
    for (const Boulder& boulder : whole) {
        wholeIds.insert(boulder.id);
    }
    for (const Boulder& boulder : part) {
        EXPECT_TRUE(wholeIds.count(boulder.id)) << boulder.id;
    }
    const auto coarse = layer.Boulders({-30.0, -30.0, 30.0, 30.0}, 0.5);
    std::set<std::uint64_t> resolved;
    for (const Boulder& boulder : whole) {
        if (boulder.diameter >= 2.0 * 0.5) {
            resolved.insert(boulder.id);
        }
    }
    std::set<std::uint64_t> kept;
    for (const Boulder& boulder : coarse) {
        if (boulder.diameter >= 1.0) {
            kept.insert(boulder.id);
        }
    }
    EXPECT_EQ(kept, resolved);
    EXPECT_LT(coarse.size(), whole.size());
}

TEST(BoulderLayer, AddsBlocksAroundFreshCraters) {
    BoulderLayerOptions options;
    options.ejecta.minCraterDiameter = 50.0;
    options.ejecta.coverage = 0.15;
    const auto craters = Craters({{0.0, 0.0, 100.0, 0.0, 0u}});
    const BoulderLayer layer(options, craters);
    EXPECT_EQ(layer.AbundanceAt(50.0, 0.0).ejecta, 1.0) << "at the rim";
    EXPECT_EQ(layer.AbundanceAt(10.0, 5.0).ejecta, 1.0) << "on the floor";
    const double halfway = layer.AbundanceAt(100.0, 0.0).ejecta;
    EXPECT_GT(halfway, 0.0);
    EXPECT_LT(halfway, 1.0);
    EXPECT_EQ(layer.AbundanceAt(160.0, 0.0).ejecta, 0.0) << "beyond 3 radii";
    EXPECT_EQ(layer.AbundanceAt(160.0, 0.0).background, 1.0);
    EXPECT_DOUBLE_EQ(layer.DensityAt(1.0, 50.0, 0.0),
                     terraingen::GolombekRappSfd(0.02).Incremental(1.0) +
                         terraingen::GolombekRappSfd(0.15).Incremental(1.0))
        << "crater blocks add to the boulders between craters";
    const auto nearRim = layer.Boulders({40.0, -10.0, 60.0, 10.0});
    const auto far = layer.Boulders({300.0, -10.0, 320.0, 10.0});
    EXPECT_GT(nearRim.size(), 3 * far.size());
    EXPECT_THROW(BoulderLayer{options}, std::invalid_argument) << "ejecta need craters";

    options.ejecta.minCraterDiameter = 200.0;
    EXPECT_EQ(BoulderLayer(options, craters).AbundanceAt(50.0, 0.0).ejecta, 0.0)
        << "smaller craters did not dig to boulder";
    options.ejecta.minCraterDiameter = 0.0;
    options.ejecta.regolithThickness = 8.0;
    EXPECT_EQ(options.ejecta.MinCraterDiameter(), 80.0) << "excavating a tenth of the diameter";
    EXPECT_EQ(BoulderLayer(options, craters).AbundanceAt(50.0, 0.0).ejecta, 1.0);
    options.ejecta.regolithThickness = 15.0;
    EXPECT_EQ(BoulderLayer(options, craters).AbundanceAt(50.0, 0.0).ejecta, 0.0);
}

TEST(BoulderLayer, ShapesAndAgesCraterBlocksLikeSynthterrainWhenAsked) {
    BoulderLayerOptions options;
    options.ejecta.minCraterDiameter = 50.0;
    options.ejecta.profile = terraingen::EjectaProfile::Exponential;
    options.ejecta.reach = 2.0;
    options.ejecta.aging = terraingen::EjectaAging::Age;
    const BoulderLayer fresh(options, Craters({{0.0, 0.0, 100.0, 0.0, 0u}}));
    EXPECT_DOUBLE_EQ(fresh.AbundanceAt(55.0, 0.0).ejecta, std::exp(-0.1 / 0.7));
    EXPECT_DOUBLE_EQ(fresh.AbundanceAt(10.0, 0.0).ejecta, 0.05) << "the floor's share";
    EXPECT_EQ(fresh.AbundanceAt(101.0, 0.0).ejecta, 0.0) << "beyond 2 radii";
    const BoulderLayer old(options, Craters({{0.0, 0.0, 100.0, 2.0, 0u}}));
    EXPECT_DOUBLE_EQ(old.AbundanceAt(55.0, 0.0).ejecta, 0.125 * std::exp(-0.1 / 0.7))
        << "(1 - 2 / 4)^3 at 2 Gyr";
    const BoulderLayer gone(options, Craters({{0.0, 0.0, 100.0, 5.0, 0u}}));
    EXPECT_EQ(gone.AbundanceAt(55.0, 0.0).ejecta, 0.0);
}

TEST(BoulderLayer, LimitsCraterBlocksToHowFarTheyAreThrownOverRegolith) {
    EXPECT_NEAR(terraingen::BoulderThrowDistance(2200.0, 4.0, 1.62), 7500.0, 100.0)
        << "Linne: about 7.5 km over 4 m of regolith (Pajola et al. 2019)";
    EXPECT_THROW(terraingen::BoulderThrowDistance(0.0, 4.0, 1.62), std::invalid_argument);
    EXPECT_THROW(terraingen::BoulderThrowDistance(10.0, 4.0, -1.0), std::invalid_argument);
    BoulderLayerOptions options;
    options.ejecta.regolithThickness = 60.0;
    options.ejecta.excavationDepthRatio = 1.0;
    const auto craters = Craters({{0.0, 0.0, 100.0, 0.0, 0u}});
    EXPECT_GT(BoulderLayer(options, craters).AbundanceAt(130.0, 0.0).ejecta, 0.0);
    options.ejecta.gravity = 1.62;
    const double thrown = terraingen::BoulderThrowDistance(100.0, 60.0, 1.62);
    ASSERT_LT(thrown, 130.0);
    EXPECT_EQ(BoulderLayer(options, craters).AbundanceAt(130.0, 0.0).ejecta, 0.0);
    EXPECT_GT(BoulderLayer(options, craters).AbundanceAt(thrown - 5.0, 0.0).ejecta, 0.0);
}

TEST(BoulderLayer, DrawsBouldersAndCraterBlocksFromAnyDistribution) {
    BoulderLayerOptions options;
    options.sizes.distribution = terraingen::SynthterrainBoulderDistribution(
        terraingen::SynthterrainBoulders::ViperSpecification);
    options.sizes.minDiameter = 1.0;
    options.sizes.maxDiameter = 8.0;
    options.ejecta.minCraterDiameter = 50.0;
    options.ejecta.distribution =
        terraingen::Sp8023CraterBlocks(terraingen::SpecificationCurve::Nominal);
    const BoulderLayer layer(options, Craters({{0.0, 0.0, 100.0, 0.0, 0u}}));
    EXPECT_DOUBLE_EQ(layer.DensityAt(2.0, 400.0, 0.0),
                     options.sizes.distribution->Incremental(2.0));
    EXPECT_DOUBLE_EQ(layer.DensityAt(2.0, 50.0, 0.0),
                     options.sizes.distribution->Incremental(2.0) +
                         options.ejecta.distribution->Incremental(2.0));
    const auto boulders = layer.Boulders({-100.0, -100.0, 100.0, 100.0});
    ASSERT_FALSE(boulders.empty());
    for (const Boulder& boulder : boulders) {
        EXPECT_GE(boulder.diameter, 1.0);
        EXPECT_LT(boulder.diameter, 8.0);
    }
    options.sizes.minDiameter = 0.1;
    EXPECT_THROW(BoulderLayer(options, Craters({})), std::invalid_argument)
        << "the crater table starts at 0.85 m";
    auto extrapolated = terraingen::Sp8023CraterBlocks(terraingen::SpecificationCurve::Nominal);
    extrapolated->SetAllowExtrapolation(true);
    options.ejecta.distribution = extrapolated;
    EXPECT_NO_THROW(BoulderLayer(options, Craters({})));
}

TEST(BoulderLayer, RaisesTheAbundanceWhereBedrockShows) {
    BoulderLayerOptions options;
    options.patches.spread = 3.0;
    options.patches.size = 1e6;
    options.sizes.coverage = 0.01;
    options.bedrock.exposure = terraingen::ExposureParams{};
    const BoulderLayer layer(options);
    const Analytic steep([](double x, double) { return static_cast<float>(0.8 * x); });
    const Analytic level([](double, double) { return 0.0f; });
    const double soil = layer.AbundanceAt(100.0, 0.0, &level).background;
    const double bedrock = layer.AbundanceAt(100.0, 0.0, &steep).background;
    EXPECT_NEAR(bedrock, 3.0 * soil, 1e-12) << "bedrockAbundance times as many";
    EXPECT_THROW(layer.AbundanceAt(0.0, 0.0), std::invalid_argument);
    EXPECT_THROW(layer.Boulders({0.0, 0.0, 1.0, 1.0}), std::invalid_argument);
}

TEST(BoulderLayer, AddsDomesThatPointQueriesMatchToTheBit) {
    BoulderLayerOptions options;
    options.sizes.coverage = 0.1;
    options.sizes.minDiameter = 0.5;
    const BoulderLayer layer(options);
    Raster<float> heights(80, 60, 1.0f);
    heights.SetTransform(GeoTransform::NorthUp(-20.0, 15.0, 0.5));
    layer.Apply(heights, {0.5, 4});
    ASSERT_FALSE(layer.Boulders(terraingen::RasterGrid::Of(heights).Bounds(), 0.5).empty());
    bool raised = false;
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const auto centre = heights.CellCenter(col, row);
            ASSERT_EQ(layer.ApplyAt(1.0f, centre.x, centre.y, {0.5}), heights(col, row));
            raised = raised || heights(col, row) > 1.0f;
        }
    }
    EXPECT_TRUE(raised);
    Raster<float> serial(80, 60, 1.0f);
    serial.SetTransform(heights.Transform());
    layer.Apply(serial, {0.5, 1});
    EXPECT_TRUE(
        std::equal(serial.Values().begin(), serial.Values().end(), heights.Values().begin()));
}

TEST(AddBoulder, IsADomeOfTheExposedHeight) {
    const Boulder boulder{0.0, 0.0, 2.0, 1.0, 0.25, 0.0, 0.0, 1u};
    EXPECT_FLOAT_EQ(terraingen::AddBoulder(5.0f, 0.0, 0.0, boulder, 1.0), 5.75f);
    EXPECT_FLOAT_EQ(terraingen::AddBoulder(5.0f, 0.6, 0.0, boulder, 1.0), 5.0f + 0.75f * 0.8f);
    EXPECT_FLOAT_EQ(terraingen::AddBoulder(5.0f, 0.0, 0.0, boulder, 0.5), 5.375f);
    EXPECT_EQ(terraingen::AddBoulder(5.0f, 1.0, 0.0, boulder, 1.0), 5.0f);
}

TEST(BoulderLayer, LeavesHeightsAloneWithoutReliefAndRejectsBadOptions) {
    BoulderLayerOptions options;
    options.sizes.coverage = 0.1;
    options.rendering.relief = terraingen::BoulderRelief::None;
    const BoulderLayer layer(options);
    Raster<float> heights(120, 120, 2.0f);
    heights.SetTransform(GeoTransform::NorthUp(0.0, 60.0, 0.5));
    layer.Apply(heights, {0.5});
    EXPECT_FALSE(layer.Boulders(terraingen::RasterGrid::Of(heights).Bounds(), 0.5).empty());
    for (const float value : heights.Values()) {
        EXPECT_EQ(value, 2.0f);
    }
    EXPECT_EQ(layer.ApplyAt(2.0f, 1.0, 1.0, {0.5}), 2.0f);
    EXPECT_TRUE(std::isnan(BoulderLayer({}).ApplyAt(std::nanf(""), 1.0, 1.0, {0.5})));
    EXPECT_THROW(BoulderLayer({}).ApplyAt(0.0f, 1.0, 1.0, {0.0}), std::invalid_argument);
    EXPECT_EQ(layer.MaxFeatureDiameter(), 4.0);
    const std::vector<std::function<void(BoulderLayerOptions&)>> changes{
        [](BoulderLayerOptions& o) { o.sizes.coverage = 0.0; },
        [](BoulderLayerOptions& o) { o.sizes.maxDiameter = 0.05; },
        [](BoulderLayerOptions& o) { o.patches.spread = 0.5; },
        [](BoulderLayerOptions& o) { o.patches.spread = 60.0; },
        [](BoulderLayerOptions& o) {
            o.ejecta.minCraterDiameter = 10.0;
            o.ejecta.coverage = 2.0;
        },
        [](BoulderLayerOptions& o) {
            o.ejecta.minCraterDiameter = 10.0;
            o.ejecta.floorShare = 2.0;
        },
        [](BoulderLayerOptions& o) {
            o.ejecta.minCraterDiameter = 10.0;
            o.ejecta.lifetimeGyr = 0.0;
        },
        [](BoulderLayerOptions& o) {
            o.ejecta.minCraterDiameter = 10.0;
            o.ejecta.gravity = 0.0;
        },
        [](BoulderLayerOptions& o) { o.ejecta.regolithThickness = -1.0; },
        [](BoulderLayerOptions& o) { o.ejecta.excavationDepthRatio = 0.0; },
        [](BoulderLayerOptions& o) { o.bedrock.abundance = 0.0; },
        [](BoulderLayerOptions& o) {
            o.bedrock.exposure = terraingen::ExposureParams{};
            o.sizes.coverage = 0.4;
        },
        [](BoulderLayerOptions& o) {
            o.sizes.distribution = std::make_shared<terraingen::PowerLawSfd>(
                1.0, 2.0, terraingen::SfdKind::ProductionRate);
        },
        [](BoulderLayerOptions& o) { o.ejecta.minCraterDiameter = -1.0; },
        [](BoulderLayerOptions& o) { o.bedrock.exposureSpacing = 0.0; },
        [](BoulderLayerOptions& o) {
            o.bedrock.exposure = terraingen::ExposureParams{.reach = 0.0};
        },
        [](BoulderLayerOptions& o) { o.pose.maxHeightRatio = 0.1; },
        [](BoulderLayerOptions& o) { o.pose.maxBurial = 1.0; },
        [](BoulderLayerOptions& o) { o.pose.maxLean = -1.0; },
        [](BoulderLayerOptions& o) { o.rendering.models.minDiameter = 0.0; },
        [](BoulderLayerOptions& o) { o.fade = {3.0, 3.0}; }};
    for (const auto& bad : changes) {
        BoulderLayerOptions invalid;
        bad(invalid);
        EXPECT_THROW(terraingen::ValidateBoulderLayerOptions(invalid), std::invalid_argument);
    }
}

TEST(BoulderModelThreshold, SplitsBouldersByDiameterOrBySamplesAtTheSpacing) {
    const terraingen::BoulderModelThreshold none;
    EXPECT_FALSE(none.IsModel(100.0, 1.0));
    const terraingen::BoulderModelThreshold big{.minDiameter = 2.0};
    EXPECT_TRUE(big.IsModel(2.0, 0.0));
    EXPECT_FALSE(big.IsModel(1.9, 0.0));
    const terraingen::BoulderModelThreshold resolved{.minSamples = 8.0};
    EXPECT_TRUE(resolved.IsModel(1.0, 0.125)) << "eight samples across at 12.5 cm";
    EXPECT_FALSE(resolved.IsModel(1.0, 0.5)) << "relief in a coarser level of detail";
    EXPECT_FALSE(resolved.IsModel(1.0, 0.0));
}

TEST(BoulderLayer, PlacesLargeBouldersAsModelsOnTheGroundAndLeavesThemOutOfTheHeights) {
    BoulderLayerOptions options;
    options.sizes.coverage = 0.15;
    options.sizes.minDiameter = 0.5;
    options.rendering.models.minDiameter = 1.0;
    const BoulderLayer layer(options);
    Raster<float> heights(160, 160, 2.0f);
    heights.SetTransform(GeoTransform::NorthUp(-40.0, 40.0, 0.5));
    const Analytic level([](double, double) { return 2.0f; });
    const auto boulders = layer.Boulders(terraingen::RasterGrid::Of(heights).Bounds(), 0.5, &level);
    std::size_t models = 0;
    for (const Boulder& boulder : boulders) {
        EXPECT_EQ(boulder.model, boulder.diameter >= 1.0);
        models += boulder.model ? 1 : 0;
        ASSERT_TRUE(boulder.ground.has_value());
        EXPECT_FLOAT_EQ(static_cast<float>(*boulder.ground), 2.0f);
    }
    ASSERT_GT(models, 0u);
    const auto modelled = layer.Models(boulders);
    EXPECT_EQ(modelled.size(), models);
    EXPECT_TRUE(layer.Models(layer.Boulders({-40.0, -40.0, 40.0, 40.0}, 0.5)).empty())
        << "no models without the ground";
    for (const auto& boulder : modelled) {
        EXPECT_TRUE(boulder.boulder.model);
        terraingen::TriangleMesh placed = boulder.shape->mesh;
        for (auto& p : placed.positions) {
            p = boulder.placement.Apply(p);
        }
        const auto box = terraingen::Bounds(placed);
        EXPECT_NEAR(box.min.z, 2.0 - boulder.boulder.burial * (box.max.z - box.min.z), 1e-9);
        EXPECT_NEAR(0.5 * (box.min.x + box.max.x), boulder.boulder.x, boulder.boulder.diameter);
    }

    options.rendering.models.minDiameter = options.sizes.minDiameter;
    Raster<float> flat(160, 160, 2.0f);
    flat.SetTransform(heights.Transform());
    BoulderLayer(options).Apply(flat, {0.5});
    for (const float value : flat.Values()) {
        ASSERT_EQ(value, 2.0f) << "models add nothing to the heights";
    }
    EXPECT_EQ(BoulderLayer(options).ApplyAt(2.0f, 1.0, 1.0, {0.5}), 2.0f);
}

TEST(BoulderLayer, ReadsTheGroundBeneathEachBoulder) {
    BoulderLayerOptions options;
    options.sizes.coverage = 0.15;
    options.sizes.minDiameter = 1.0;
    options.rendering.models.minDiameter = 1.0;
    const BoulderLayer layer(options);
    const Analytic slope([](double x, double) { return static_cast<float>(0.1 * x); });
    const auto boulders = layer.Boulders({0.0, 0.0, 40.0, 40.0}, 0.5, &slope);
    ASSERT_FALSE(boulders.empty());
    for (const Boulder& boulder : boulders) {
        ASSERT_TRUE(boulder.ground.has_value());
        EXPECT_NEAR(*boulder.ground, 0.1 * boulder.x, 1e-5);
    }
    EXPECT_FALSE(layer.Boulders({0.0, 0.0, 40.0, 40.0}, 0.5).front().ground.has_value());
}

TEST(BoulderLayer, DrawsTheTopsOfBoulderShapesThatPointQueriesMatch) {
    BoulderLayerOptions options;
    options.sizes.coverage = 0.1;
    options.sizes.minDiameter = 0.5;
    options.rendering.relief = terraingen::BoulderRelief::Shape;
    options.rendering.shapes.subdivisions = 2;
    const BoulderLayer layer(options);
    Raster<float> heights(60, 50, 1.0f);
    heights.SetTransform(GeoTransform::NorthUp(-15.0, 12.5, 0.5));
    layer.Apply(heights, {0.5, 3});
    const auto boulders = layer.Boulders(terraingen::RasterGrid::Of(heights).Bounds(), 0.5);
    ASSERT_FALSE(boulders.empty());
    double highest = 0.0;
    double tallest = 0.0;
    for (const Boulder& boulder : boulders) {
        tallest = std::fmax(tallest, boulder.ExposedHeight());
    }
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const auto centre = heights.CellCenter(col, row);
            ASSERT_EQ(layer.ApplyAt(1.0f, centre.x, centre.y, {0.5}), heights(col, row));
            highest = std::fmax(highest, static_cast<double>(heights(col, row)) - 1.0);
        }
    }
    EXPECT_GT(highest, 0.0);
    EXPECT_LE(highest, 2.0 * tallest) << "overlapping boulders add up";
    std::vector<terraingen::Vec2d> centres;
    for (std::size_t i = 0; i < heights.Values().size(); ++i) {
        centres.push_back(heights.CellCenter(i % heights.Width(), i / heights.Width()));
    }
    std::vector<float> batch(centres.size(), 1.0f);
    terraingen::SampleBlock points(centres, batch);
    layer.Apply(points, {0.5});
    EXPECT_TRUE(std::equal(batch.begin(), batch.end(), heights.Values().begin()))
        << "one batch of points matches the grid";
    options.rendering.relief = terraingen::BoulderRelief::Dome;
    Raster<float> domes(60, 50, 1.0f);
    domes.SetTransform(heights.Transform());
    BoulderLayer(options).Apply(domes, {0.5});
    EXPECT_FALSE(std::equal(domes.Values().begin(), domes.Values().end(), heights.Values().begin()))
        << "shapes are not domes";
}

TEST(BoulderLayer, SharesShapesBetweenBouldersOfOnePrototype) {
    BoulderLayerOptions options;
    options.rendering.shapePrototypes = 1;
    const BoulderLayer layer(options);
    EXPECT_EQ(layer.Shape(Boulder{.id = 1}).get(), layer.Shape(Boulder{.id = 99}).get());
    options.rendering.shapePrototypes = 0;
    const BoulderLayer unique(options);
    EXPECT_NE(unique.Shape(Boulder{.id = 1})->mesh.positions,
              unique.Shape(Boulder{.id = 99})->mesh.positions);
    options.rendering.models.minSamples = -1.0;
    EXPECT_THROW(BoulderLayer{options}, std::invalid_argument);
}

TEST(FilletHeight, IsLevelToTheRadiusThenFallsToNothingAtItsReach) {
    const Boulder boulder{0.0, 0.0, 2.0, 1.0, 0.25, 0.0, 0.0, 1u};
    const terraingen::BoulderFillet fillet{0.4, 0.25};
    EXPECT_DOUBLE_EQ(terraingen::FilletHeight(boulder, 0.0, fillet), 0.3);
    EXPECT_DOUBLE_EQ(terraingen::FilletHeight(boulder, 1.0, fillet), 0.3);
    EXPECT_DOUBLE_EQ(terraingen::FilletHeight(boulder, 1.25, fillet), 0.075);
    EXPECT_EQ(terraingen::FilletHeight(boulder, 1.5, fillet), 0.0);
    EXPECT_DOUBLE_EQ(terraingen::FilletReach(boulder, fillet), 1.5);
    EXPECT_THROW(terraingen::ValidateBoulderFillet({1.5, 0.25}), std::invalid_argument);
    EXPECT_THROW(terraingen::ValidateBoulderFillet({0.4, 0.0}), std::invalid_argument);
}

TEST(BoulderLayer, PilesFilletsAroundBouldersThatPointQueriesMatch) {
    BoulderLayerOptions options;
    options.sizes.coverage = 0.05;
    options.sizes.minDiameter = 0.5;
    const BoulderLayer bare(options);
    options.fillet = {0.3, 0.3};
    const BoulderLayer filleted(options);
    Raster<float> plain(80, 60, 1.0f);
    plain.SetTransform(GeoTransform::NorthUp(-20.0, 15.0, 0.25));
    Raster<float> piled = plain;
    bare.Apply(plain, {0.25, 4});
    filleted.Apply(piled, {0.25, 4});
    const auto boulders = filleted.Boulders(terraingen::RasterGrid::Of(piled).Bounds(), 0.25);
    ASSERT_FALSE(boulders.empty());
    std::size_t aprons = 0;
    for (std::size_t row = 0; row < piled.Height(); ++row) {
        for (std::size_t col = 0; col < piled.Width(); ++col) {
            const auto centre = piled.CellCenter(col, row);
            ASSERT_EQ(filleted.ApplyAt(1.0f, centre.x, centre.y, {0.25}), piled(col, row));
            ASSERT_GE(piled(col, row), plain(col, row));
            aprons += plain(col, row) == 1.0f && piled(col, row) > 1.0f ? 1u : 0u;
            bool near = false;
            for (const Boulder& boulder : boulders) {
                near = near || std::hypot(centre.x - boulder.x, centre.y - boulder.y) <
                                   terraingen::FilletReach(boulder, options.fillet);
            }
            if (!near) {
                ASSERT_EQ(piled(col, row), 1.0f) << "beyond every fillet";
            }
        }
    }
    EXPECT_GT(aprons, 0u) << "fillets reach past the domes";

    options.rendering.relief = terraingen::BoulderRelief::None;
    options.rendering.models.minDiameter = options.sizes.minDiameter;
    Raster<float> modelled(80, 60, 1.0f);
    modelled.SetTransform(plain.Transform());
    BoulderLayer(options).Apply(modelled, {0.25});
    EXPECT_GT(*std::max_element(modelled.Values().begin(), modelled.Values().end()), 1.0f)
        << "models stand in fillets too";
}

TEST(BoulderLayer, SetsBurialByWearWhenAsked) {
    BoulderLayerOptions options;
    options.ejecta.minCraterDiameter = 50.0;
    options.ejecta.coverage = 0.15;
    options.pose.burialByWear = 1.0;
    const auto craters = Craters({{0.0, 0.0, 100.0, 0.0, 0u}});
    const BoulderLayer layer(options, craters);
    EXPECT_EQ(layer.AbundanceAt(50.0, 0.0).ejectaWear, 0.0) << "a fresh crater";
    std::size_t fresh = 0;
    const auto nearRim = layer.Boulders({40.0, -10.0, 60.0, 10.0});
    for (const Boulder& boulder : nearRim) {
        fresh += boulder.burial == 0.1 ? 1u : 0u;
    }
    EXPECT_GT(fresh, nearRim.size() / 2) << "most blocks at the rim are the fresh crater's";
    double least = 1.0;
    double most = 0.0;
    for (const Boulder& boulder : layer.Boulders({300.0, -10.0, 320.0, 10.0})) {
        least = std::fmin(least, boulder.burial);
        most = std::fmax(most, boulder.burial);
    }
    EXPECT_LT(least, 0.15) << "boulders between craters span every stage of wear";
    EXPECT_GT(most, 0.35);
    options.pose.burialByWear = 1.5;
    EXPECT_THROW(BoulderLayer(options, craters), std::invalid_argument);
}
