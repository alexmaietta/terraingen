// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/boulders/boulder_layer.h"

#include "support/boulder_records.h"
#include "support/golombek_rapp.h"
#include "terraingen/random/stream.h"
#include "terraingen/terraingen.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/span.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

using terraingen::Boulder;
using terraingen::BoulderLayer;
using terraingen::BoulderLayerOptions;
using terraingen::Bounds2d;
using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::RasterGrid;

namespace {

/// Craters with no generation, only `manual` ones, fresh.
std::shared_ptr<const terraingen::CraterLayer>
Craters(const std::vector<terraingen::Crater>& manual) {
    terraingen::CraterLayerOptions options;
    options.generateRandom = false;
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

  protected:
    void FillSamples(terraingen::SampleBlock& samples, double /*spacing*/,
                     unsigned /*threads*/) const override {
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            const terraingen::Vec2d p = samples.Position(i);
            samples.Height(i) = height_(p.x, p.y);
        }
    }

  private:
    std::function<float(double, double)> height_;
};

}  // namespace

TEST(BoulderLayer, GivesEachBoulderStableAttributesInTheirRanges) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.05;
    const BoulderLayer layer(options);
    const auto boulders = terraingen::testing::Records(layer.Boulders({-20.0, -20.0, 20.0, 20.0}));
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
        EXPECT_LT(boulder.yaw, 2.0 * terraingen::kPi);
        EXPECT_LE(boulder.lean, 0.26);
        EXPECT_DOUBLE_EQ(boulder.ExposedHeight(), boulder.height * (1.0 - boulder.burial));
        ids.insert(boulder.id);
    }
    EXPECT_EQ(ids.size(), boulders.size());
    EXPECT_EQ(terraingen::testing::Records(layer.Boulders({-20.0, -20.0, 20.0, 20.0})), boulders);
}

TEST(BoulderLayer, GivesTheSameBouldersInEveryRegionAndSkipsOnlyUnresolvedOnes) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.05;
    options.patches = {2.0, 30.0};
    const BoulderLayer layer(options);
    const auto whole = terraingen::testing::Records(layer.Boulders({-30.0, -30.0, 30.0, 30.0}));
    const auto part = terraingen::testing::Records(layer.Boulders({-5.0, 0.0, 10.0, 12.0}));
    std::set<std::uint64_t> wholeIds;
    for (const Boulder& boulder : whole) {
        wholeIds.insert(boulder.id);
    }
    for (const Boulder& boulder : part) {
        EXPECT_TRUE(wholeIds.count(boulder.id)) << boulder.id;
    }
    const auto coarse =
        terraingen::testing::Records(layer.Boulders({-30.0, -30.0, 30.0, 30.0}, 0.5));
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

TEST(BoulderLayer, TakesBlocksFromEveryCraterLayerGiven) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.ejecta.minCraterDiameter = 50.0;
    const auto first = Craters({{0.0, 0.0, 100.0, 0.0, 0u}});
    const auto second = Craters({{1000.0, 0.0, 100.0, 0.0, 0u}});
    const BoulderLayer both(options, {first, second}, terraingen::Surface{});
    EXPECT_EQ(both.AbundanceAt(50.0, 0.0).ejecta, 1.0) << "the first layer's rim";
    EXPECT_EQ(both.AbundanceAt(1050.0, 0.0).ejecta, 1.0) << "the second layer's rim";
    const BoulderLayer last(options, second);
    EXPECT_EQ(last.AbundanceAt(50.0, 0.0).ejecta, 0.0);
    const BoulderLayer one(options, {first}, terraingen::Surface{});
    const BoulderLayer single(options, first);
    EXPECT_EQ(terraingen::testing::Records(one.Boulders({30.0, -20.0, 70.0, 20.0})),
              terraingen::testing::Records(single.Boulders({30.0, -20.0, 70.0, 20.0})))
        << "one layer in a list is that layer";
    EXPECT_THROW(
        BoulderLayer(options,
                     std::vector<std::shared_ptr<const terraingen::CraterLayer>>{first, nullptr},
                     terraingen::Surface{}),
        std::invalid_argument);
}

TEST(BoulderLayer, ReseedsItsPlacementsAndShapes) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.05;
    const BoulderLayer layer(options);
    const Bounds2d region{-20.0, -20.0, 20.0, 20.0};
    const auto records = [&](const terraingen::LayerPtr& reseeded) {
        return terraingen::testing::Records(
            dynamic_cast<const BoulderLayer&>(*reseeded).Boulders(region));
    };
    const auto a = records(layer.Reseeded(9));
    EXPECT_EQ(a, records(layer.Reseeded(9)));
    EXPECT_NE(a, records(layer.Reseeded(10)));
    EXPECT_NE(a, terraingen::testing::Records(layer.Boulders(region)));
    const terraingen::LayerPtr held = layer.Reseeded(9);
    const auto& reseeded = dynamic_cast<const BoulderLayer&>(*held);
    EXPECT_EQ(reseeded.Options().seed, terraingen::ReseedWord(9, 0));
    EXPECT_EQ(reseeded.Options().rendering.shapes.seed, terraingen::ReseedWord(9, 1));
}

TEST(BoulderLayer, RebindsToAReplacedCraterLayer) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.ejecta.minCraterDiameter = 50.0;
    const auto first = Craters({{0.0, 0.0, 100.0, 0.0, 0u}});
    const auto moved = Craters({{1000.0, 0.0, 100.0, 0.0, 0u}});
    const BoulderLayer layer(options, first);
    EXPECT_EQ(layer.Rebound({}), nullptr);
    const auto unrelated = Craters({});
    EXPECT_EQ(layer.Rebound({{unrelated.get(), moved}}), nullptr) << "reads none of them";
    const terraingen::LayerPtr rebound = layer.Rebound({{first.get(), moved}});
    ASSERT_NE(rebound, nullptr);
    EXPECT_EQ(rebound->Dependencies(), std::vector<const terraingen::Layer*>{moved.get()});
    const auto& boulders = dynamic_cast<const BoulderLayer&>(*rebound);
    EXPECT_EQ(boulders.AbundanceAt(1050.0, 0.0).ejecta, 1.0);
    EXPECT_EQ(boulders.AbundanceAt(50.0, 0.0).ejecta, 0.0);
}

TEST(BoulderLayer, AddsBlocksAroundFreshCraters) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
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
    const auto nearRim = terraingen::testing::Records(layer.Boulders({40.0, -10.0, 60.0, 10.0}));
    const auto far = terraingen::testing::Records(layer.Boulders({300.0, -10.0, 320.0, 10.0}));
    EXPECT_GT(nearRim.size(), 3 * far.size());
    EXPECT_THROW(BoulderLayer{options}, std::invalid_argument) << "ejecta need craters";

    options.ejecta.minCraterDiameter = 200.0;
    EXPECT_EQ(BoulderLayer(options, craters).AbundanceAt(50.0, 0.0).ejecta, 0.0)
        << "smaller craters did not dig to boulder";
    options.ejecta.minCraterDiameter = 0.0;
    options.ejecta.regolithThickness = 8.0;
    EXPECT_EQ(options.ejecta.ThrowingDiameter(), 80.0) << "excavating a tenth of the diameter";
    EXPECT_EQ(BoulderLayer(options, craters).AbundanceAt(50.0, 0.0).ejecta, 1.0);
    options.ejecta.regolithThickness = 15.0;
    EXPECT_EQ(BoulderLayer(options, craters).AbundanceAt(50.0, 0.0).ejecta, 0.0);
}

TEST(BoulderLayer, ThrowsNoCraterBlocksWithoutRandomBoulders) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.generateRandom = false;
    options.ejecta.minCraterDiameter = 50.0;
    options.ejecta.coverage = 0.15;
    terraingen::ManualBoulder rim;
    rim.x = 50.0;
    rim.diameter = 1.0;
    options.manual = {rim};
    const BoulderLayer layer(options, Craters({{0.0, 0.0, 100.0, 0.0, 0u}}));
    EXPECT_EQ(layer.AbundanceAt(50.0, 0.0).ejecta, 0.0);
    const auto boulders = terraingen::testing::Records(layer.Boulders({40.0, -10.0, 60.0, 10.0}));
    ASSERT_EQ(boulders.size(), 1u) << "the manual boulder alone, no blocks at the rim";
    EXPECT_EQ(boulders[0].id, terraingen::ManualBoulderId(rim));
    EXPECT_NO_THROW(BoulderLayer{options}) << "and no crater layer needed";
}

TEST(BoulderLayer, ShapesAndAgesCraterBlocksLikeSynthterrainWhenAsked) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
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
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
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
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.distribution =
        std::make_shared<terraingen::PowerLawSfd>(terraingen::SynthterrainBoulderDistribution(
            terraingen::SynthterrainBoulders::ViperSpecification));
    options.sizes.minDiameter = 1.0;
    options.sizes.maxDiameter = 8.0;
    options.ejecta.minCraterDiameter = 50.0;
    options.ejecta.distribution = std::make_shared<terraingen::TabulatedSfd>(
        terraingen::Sp8023CraterBlocks(terraingen::SpecificationCurve::Nominal));
    const BoulderLayer layer(options, Craters({{0.0, 0.0, 100.0, 0.0, 0u}}));
    const auto boulders =
        terraingen::testing::Records(layer.Boulders({-100.0, -100.0, 100.0, 100.0}));
    ASSERT_FALSE(boulders.empty());
    for (const Boulder& boulder : boulders) {
        EXPECT_GE(boulder.diameter, 1.0);
        EXPECT_LT(boulder.diameter, 8.0);
    }
    options.sizes.minDiameter = 0.1;
    EXPECT_THROW(BoulderLayer(options, Craters({})), std::invalid_argument)
        << "the crater table starts at 0.85 m";
    auto extrapolated = std::make_shared<terraingen::TabulatedSfd>(
        terraingen::Sp8023CraterBlocks(terraingen::SpecificationCurve::Nominal));
    extrapolated->SetAllowExtrapolation(true);
    options.ejecta.distribution = extrapolated;
    EXPECT_NO_THROW(BoulderLayer(options, Craters({})));
}

TEST(BoulderLayer, RaisesTheAbundanceWhereBedrockShows) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
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
    EXPECT_THROW(terraingen::testing::Records(layer.Boulders({0.0, 0.0, 1.0, 1.0})),
                 std::invalid_argument);
}

TEST(BoulderLayer, AddsDomesThatPointQueriesMatchToTheBit) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.1;
    options.sizes.minDiameter = 0.5;
    const BoulderLayer layer(options);
    Raster<float> heights(RasterGrid{80, 60, GeoTransform::NorthUp(-20.0, 15.0, 0.5)}, 1.0f);
    layer.Apply(heights, {0.5, 4});
    ASSERT_FALSE(
        terraingen::testing::Records(layer.Boulders(heights.Grid().Bounds(), 0.5)).empty());
    bool raised = false;
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const auto center = heights.CellCenter(col, row);
            ASSERT_EQ(layer.ApplyAt(1.0f, center.x, center.y, {0.5}), heights(col, row));
            raised = raised || heights(col, row) > 1.0f;
        }
    }
    EXPECT_TRUE(raised);
    Raster<float> serial(RasterGrid{80, 60, heights.Transform()}, 1.0f);
    layer.Apply(serial, {0.5, 1});
    EXPECT_TRUE(
        std::equal(serial.Values().begin(), serial.Values().end(), heights.Values().begin()));
}

TEST(BoulderLayer, LeavesHeightsAloneWithoutReliefAndRejectsBadOptions) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.1;
    options.rendering.relief = terraingen::BoulderRelief::None;
    const BoulderLayer layer(options);
    Raster<float> heights(RasterGrid{120, 120, GeoTransform::NorthUp(0.0, 60.0, 0.5)}, 2.0f);
    layer.Apply(heights, {0.5});
    EXPECT_FALSE(
        terraingen::testing::Records(layer.Boulders(heights.Grid().Bounds(), 0.5)).empty());
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
        [](BoulderLayerOptions& o) { o.groundSpacing = 0.0; },
        [](BoulderLayerOptions& o) { o.bedrock.exposure = terraingen::ExposureParams{0.0}; },
        [](BoulderLayerOptions& o) { o.pose.maxHeightRatio = 0.1; },
        [](BoulderLayerOptions& o) { o.pose.maxBurial = 1.0; },
        [](BoulderLayerOptions& o) { o.pose.maxLeanDegrees = (-1.0) * terraingen::kRadian; },
        [](BoulderLayerOptions& o) { o.rendering.modelThreshold.minDiameter = 0.0; },
        [](BoulderLayerOptions& o) { o.fade = {3.0, 3.0}; }};
    for (const auto& bad : changes) {
        BoulderLayerOptions invalid = terraingen::testing::GolombekRappBoulders();
        bad(invalid);
        EXPECT_THROW(terraingen::ValidateBoulderLayerOptions(invalid), std::invalid_argument);
    }
}

TEST(BoulderModelThreshold, SplitsBouldersByDiameterOrBySamplesAtTheSpacing) {
    const terraingen::BoulderModelThreshold none;
    EXPECT_FALSE(none.IsModel(100.0, 1.0));
    const terraingen::BoulderModelThreshold big{2.0};
    EXPECT_TRUE(big.IsModel(2.0, 0.0));
    EXPECT_FALSE(big.IsModel(1.9, 0.0));
    terraingen::BoulderModelThreshold resolved;
    resolved.minPixels = 8.0;
    EXPECT_TRUE(resolved.IsModel(1.0, 0.125)) << "eight samples across at 12.5 cm";
    EXPECT_FALSE(resolved.IsModel(1.0, 0.5)) << "relief in a coarser level of detail";
    EXPECT_FALSE(resolved.IsModel(1.0, 0.0));
}

TEST(BoulderLayer, PlacesLargeBouldersAsModelsOnTheGroundAndLeavesThemOutOfTheHeights) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.15;
    options.sizes.minDiameter = 0.5;
    options.rendering.modelThreshold.minDiameter = 1.0;
    const BoulderLayer layer(options);
    Raster<float> heights(RasterGrid{160, 160, GeoTransform::NorthUp(-40.0, 40.0, 0.5)}, 2.0f);
    const Analytic level([](double, double) { return 2.0f; });
    const auto boulders =
        terraingen::testing::Records(layer.Boulders(heights.Grid().Bounds(), 0.5, &level));
    std::size_t models = 0;
    for (const Boulder& boulder : boulders) {
        EXPECT_EQ(boulder.model, boulder.diameter >= 1.0);
        models += boulder.model ? 1 : 0;
        ASSERT_TRUE(boulder.ground.has_value());
        EXPECT_FLOAT_EQ(static_cast<float>(*boulder.ground), 2.0f);
    }
    ASSERT_GT(models, 0u);
    const auto modeled = layer.Models(boulders);
    EXPECT_EQ(modeled.size(), models);
    EXPECT_TRUE(
        layer.Models(terraingen::testing::Records(layer.Boulders({-40.0, -40.0, 40.0, 40.0}, 0.5)))
            .empty())
        << "no models without the ground";
    for (const auto& boulder : modeled) {
        EXPECT_TRUE(boulder.boulder.model);
        terraingen::TriangleMesh placed = boulder.shape->mesh;
        for (auto& p : placed.positions) {
            p = boulder.placement.Apply(p);
        }
        const auto box = terraingen::Bounds(placed);
        EXPECT_NEAR(box.min.z, 2.0 - boulder.boulder.burial * (box.max.z - box.min.z), 1e-9);
        EXPECT_NEAR(0.5 * (box.min.x + box.max.x), boulder.boulder.x, boulder.boulder.diameter);
    }

    options.rendering.modelThreshold.minDiameter = options.sizes.minDiameter;
    Raster<float> flat(RasterGrid{160, 160, heights.Transform()}, 2.0f);
    BoulderLayer(options).Apply(flat, {0.5});
    for (const float value : flat.Values()) {
        ASSERT_EQ(value, 2.0f) << "models add nothing to the heights";
    }
    EXPECT_EQ(BoulderLayer(options).ApplyAt(2.0f, 1.0, 1.0, {0.5}), 2.0f);
}

TEST(BoulderLayer, ReadsTheGroundBeneathEachBoulder) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.15;
    options.sizes.minDiameter = 1.0;
    options.rendering.modelThreshold.minDiameter = 1.0;
    const BoulderLayer layer(options);
    const Analytic slope([](double x, double) { return static_cast<float>(0.1 * x); });
    const auto boulders =
        terraingen::testing::Records(layer.Boulders({0.0, 0.0, 40.0, 40.0}, 0.5, &slope));
    ASSERT_FALSE(boulders.empty());
    for (const Boulder& boulder : boulders) {
        ASSERT_TRUE(boulder.ground.has_value());
        EXPECT_NEAR(*boulder.ground, 0.1 * boulder.x, 1e-5);
    }
    EXPECT_FALSE(terraingen::testing::Records(layer.Boulders({0.0, 0.0, 40.0, 40.0}, 0.5))
                     .front()
                     .ground.has_value());
}

TEST(BoulderLayer, DrawsTheTopsOfBoulderShapesThatPointQueriesMatch) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.1;
    options.sizes.minDiameter = 0.5;
    options.rendering.relief = terraingen::BoulderRelief::Shape;
    options.rendering.shapes.subdivisions = 2;
    const BoulderLayer layer(options);
    Raster<float> heights(RasterGrid{60, 50, GeoTransform::NorthUp(-15.0, 12.5, 0.5)}, 1.0f);
    layer.Apply(heights, {0.5, 3});
    const auto boulders =
        terraingen::testing::Records(layer.Boulders(heights.Grid().Bounds(), 0.5));
    ASSERT_FALSE(boulders.empty());
    double highest = 0.0;
    double tallest = 0.0;
    for (const Boulder& boulder : boulders) {
        tallest = std::fmax(tallest, boulder.ExposedHeight());
    }
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const auto center = heights.CellCenter(col, row);
            ASSERT_EQ(layer.ApplyAt(1.0f, center.x, center.y, {0.5}), heights(col, row));
            highest = std::fmax(highest, static_cast<double>(heights(col, row)) - 1.0);
        }
    }
    EXPECT_GT(highest, 0.0);
    EXPECT_LE(highest, 2.0 * tallest) << "overlapping boulders add up";
    std::vector<terraingen::Vec2d> centers;
    for (std::size_t i = 0; i < heights.Values().size(); ++i) {
        centers.push_back(heights.CellCenter(i % heights.Width(), i / heights.Width()));
    }
    std::vector<float> batch(centers.size(), 1.0f);
    terraingen::SampleBlock points(centers, batch);
    layer.Apply(points, {0.5});
    EXPECT_TRUE(std::equal(batch.begin(), batch.end(), heights.Values().begin()))
        << "one batch of points matches the grid";
    options.rendering.relief = terraingen::BoulderRelief::Dome;
    Raster<float> domes(RasterGrid{60, 50, heights.Transform()}, 1.0f);
    BoulderLayer(options).Apply(domes, {0.5});
    EXPECT_FALSE(std::equal(domes.Values().begin(), domes.Values().end(), heights.Values().begin()))
        << "shapes are not domes";
}

TEST(BoulderLayer, SharesShapesBetweenBouldersOfOnePrototype) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.rendering.shapePrototypes = 1;
    const BoulderLayer layer(options);
    Boulder first;
    first.id = 1;
    Boulder other;
    other.id = 99;
    EXPECT_EQ(layer.Shape(first).get(), layer.Shape(other).get());
    options.rendering.shapePrototypes = 0;
    const BoulderLayer unique(options);
    EXPECT_NE(unique.Shape(first)->mesh.positions, unique.Shape(other)->mesh.positions);
    options.rendering.modelThreshold.minPixels = -1.0;
    EXPECT_THROW(BoulderLayer{options}, std::invalid_argument);
}

TEST(FilletHeight, RisesFromItsReachToWhereTheDomeStandsAsHighThenGivesWayToTheRock) {
    const Boulder boulder{0.0, 0.0, 2.0, 1.0, 0.25, 0.0, 0.0, 1u};
    const terraingen::BoulderFillet fillet{0.4, 0.25};
    const terraingen::FilletContact contact = terraingen::DomeFilletContact(boulder, fillet);
    EXPECT_DOUBLE_EQ(contact.distance, std::sqrt(1.0 - 0.4 * 0.4));
    EXPECT_DOUBLE_EQ(contact.height, 0.3);
    EXPECT_EQ(terraingen::FilletHeight(boulder, 0.0, fillet), 0.0) << "under the rock";
    EXPECT_EQ(terraingen::FilletHeight(boulder, 0.9 * contact.distance, fillet), 0.0);
    EXPECT_DOUBLE_EQ(terraingen::FilletHeight(boulder, contact.distance, fillet), 0.3);
    const double t = (1.25 - contact.distance) / (1.5 - contact.distance);
    EXPECT_DOUBLE_EQ(terraingen::FilletHeight(boulder, 1.25, fillet), 0.3 * (1.0 - t) * (1.0 - t));
    EXPECT_EQ(terraingen::FilletHeight(boulder, 1.5, fillet), 0.0);
    EXPECT_DOUBLE_EQ(terraingen::FilletReach(boulder, fillet), 1.5);
    const double dome =
        boulder.ExposedHeight() * std::sqrt(1.0 - contact.distance * contact.distance);
    EXPECT_NEAR(dome, contact.height, 1e-12) << "it meets the dome's flank at its own height";

    const terraingen::FilletContact wide{1.2, 0.2};
    EXPECT_EQ(terraingen::FilletHeight(boulder, 1.1, fillet, wide), 0.0);
    EXPECT_DOUBLE_EQ(terraingen::FilletHeight(boulder, 1.2, fillet, wide), 0.2);
    EXPECT_DOUBLE_EQ(terraingen::FilletHeight(boulder, 1.45, fillet, wide), 0.05)
        << "it reaches its width beyond a contact past the radius";
    EXPECT_EQ(terraingen::FilletHeight(boulder, 1.7, fillet, wide), 0.0);
    EXPECT_THROW(terraingen::ValidateBoulderFillet({1.5, 0.25}), std::invalid_argument);
    EXPECT_THROW(terraingen::ValidateBoulderFillet({0.4, 0.0}), std::invalid_argument);
}

namespace {

/// A layer of one unburied 2 m boulder 1 m high at the origin with a fillet, drawn as `relief`;
/// a model with `model`, cut by a facet facing west with `facets`.
std::shared_ptr<BoulderLayer> FilletedBoulder(terraingen::BoulderRelief relief, double share,
                                              bool model = false, bool facets = false) {
    BoulderLayerOptions options;
    options.rendering.relief = relief;
    options.fillet = {share, 0.3};
    if (facets) {
        options.ventifacts.share = 1.0;
        options.ventifacts.windAzimuthDegrees = 0.0;
    }
    terraingen::ManualBoulder boulder(0.0, 0.0, 2.0);
    boulder.Height(1.0).Burial(0.0).YawDegrees(0.0).LeanDegrees(0.0).TiltDegrees(0.0).Wear(0.5);
    boulder.Model(model);
    return BoulderLayer::Manual({boulder}, options);
}

/// Heights of `layer` on level ground at 0, every `step` meters from `from` to `to` meters out
/// from the origin toward `azimuth` (radians from east), drawn at `step` spacing.
std::vector<float> RadialProfile(const BoulderLayer& layer, double azimuth, double from, double to,
                                 double step) {
    std::vector<float> heights;
    for (double d = from; d <= to + 1e-9; d += step) {
        heights.push_back(
            layer.ApplyAt(0.0f, d * std::cos(azimuth), d * std::sin(azimuth), {step}));
    }
    return heights;
}

/// The longest run of steps of `profile`, `step` meters apart, whose ends both stand between `low`
/// and `high` meters and whose slope is under `slope`: a bench.
std::size_t LongestBench(const std::vector<float>& profile, double step, double low, double high,
                         double slope) {
    std::size_t longest = 0;
    std::size_t run = 0;
    for (std::size_t i = 0; i + 1 < profile.size(); ++i) {
        const double a = profile[i];
        const double b = profile[i + 1];
        const bool level =
            a > low && b > low && a < high && b < high && std::fabs(b - a) < slope * step;
        run = level ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    return longest;
}

}  // namespace

TEST(BoulderLayer, FilletsRampIntoDomesWithNoBenchAtTheContact) {
    const double step = 0.01;
    const auto layer = FilletedBoulder(terraingen::BoulderRelief::Dome, 0.5);
    for (const double azimuth : {0.0, 1.0, 2.5, 4.0}) {
        const std::vector<float> profile = RadialProfile(*layer, azimuth, 0.5, 1.6, step);
        for (std::size_t i = 0; i + 1 < profile.size(); ++i) {
            ASSERT_LE(profile[i + 1], profile[i] + 1e-5f)
                << "heights fall from the flank down the fillet at " << 0.5 + step * i << " m";
        }
        EXPECT_GT(profile[static_cast<std::size_t>(0.6 / step)], 0.1f) << "the fillet at 1.1 m";
        EXPECT_LE(LongestBench(profile, step, 0.02, 2.0, 0.05), 1u)
            << "no level ring where the fillet meets the dome, azimuth " << azimuth;
    }
}

TEST(BoulderLayer, FilletsRampIntoFacetedDomesWithNoBenchAtTheContact) {
    const double step = 0.01;
    const auto layer = FilletedBoulder(terraingen::BoulderRelief::Dome, 0.5, false, true);
    const auto records = terraingen::testing::Records(layer->Boulders({-3.0, -3.0, 3.0, 3.0}));
    ASSERT_EQ(records.size(), 1u);
    ASSERT_EQ(static_cast<int>(records[0].facetCount), 1);
    for (const double azimuth : {0.0, 2.5, terraingen::kPi, 4.0}) {
        const std::vector<float> profile = RadialProfile(*layer, azimuth, 0.0, 1.6, step);
        EXPECT_LE(LongestBench(profile, step, 0.02, 0.52, 0.05), 1u)
            << "no shelf at the fillet's height over the facet, azimuth " << azimuth;
    }
}

TEST(BoulderLayer, FilletsRampIntoShapesAndModelsWithNoBench) {
    const double step = 0.01;
    for (const bool model : {false, true}) {
        const auto relief =
            model ? terraingen::BoulderRelief::None : terraingen::BoulderRelief::Shape;
        const auto filleted = FilletedBoulder(relief, 0.4, model);
        const auto bare = FilletedBoulder(relief, 0.0, model);
        if (model) {
            EXPECT_EQ(filleted->ApplyAt(0.0f, 0.0, 0.0, {step}), 0.0f)
                << "no fillet under the model";
        }
        std::size_t seen = 0;
        for (int k = 0; k < 16; ++k) {
            const double azimuth = 2.0 * terraingen::kPi * k / 16.0;
            const std::vector<float> piled = RadialProfile(*filleted, azimuth, 0.0, 1.8, step);
            const std::vector<float> rock = RadialProfile(*bare, azimuth, 0.0, 1.8, step);
            for (std::size_t i = 0; i + 1 < piled.size(); ++i) {
                const bool shown = piled[i] > rock[i] + 1e-4f && piled[i + 1] > rock[i + 1] + 1e-4f;
                if (!shown || piled[i] < 0.02f) {
                    continue;
                }
                ++seen;
                ASSERT_GE(piled[i] - piled[i + 1], 0.02 * step)
                    << "the fillet falls away from the boulder wherever it shows, azimuth "
                    << azimuth << " at " << step * i << " m" << (model ? " around a model" : "");
            }
        }
        EXPECT_GT(seen, 100u) << "the fillet shows around the boulder";
    }
}

TEST(BoulderLayer, PilesFilletsAroundBouldersThatPointQueriesMatch) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.05;
    options.sizes.minDiameter = 0.5;
    const BoulderLayer bare(options);
    options.fillet = {0.3, 0.3};
    const BoulderLayer filleted(options);
    Raster<float> plain(RasterGrid{80, 60, GeoTransform::NorthUp(-20.0, 15.0, 0.25)}, 1.0f);
    Raster<float> piled = plain;
    bare.Apply(plain, {0.25, 4});
    filleted.Apply(piled, {0.25, 4});
    const auto boulders =
        terraingen::testing::Records(filleted.Boulders(piled.Grid().Bounds(), 0.25));
    ASSERT_FALSE(boulders.empty());
    std::size_t aprons = 0;
    for (std::size_t row = 0; row < piled.Height(); ++row) {
        for (std::size_t col = 0; col < piled.Width(); ++col) {
            const auto center = piled.CellCenter(col, row);
            ASSERT_EQ(filleted.ApplyAt(1.0f, center.x, center.y, {0.25}), piled(col, row));
            ASSERT_GE(piled(col, row), plain(col, row));
            aprons += plain(col, row) == 1.0f && piled(col, row) > 1.0f ? 1u : 0u;
            bool near = false;
            for (const Boulder& boulder : boulders) {
                near = near || std::hypot(center.x - boulder.x, center.y - boulder.y) <
                                   terraingen::FilletReach(boulder, options.fillet);
            }
            if (!near) {
                ASSERT_EQ(piled(col, row), 1.0f) << "beyond every fillet";
            }
        }
    }
    EXPECT_GT(aprons, 0u) << "fillets reach past the domes";

    options.rendering.relief = terraingen::BoulderRelief::None;
    options.rendering.modelThreshold.minDiameter = options.sizes.minDiameter;
    Raster<float> modeled(RasterGrid{80, 60, plain.Transform()}, 1.0f);
    BoulderLayer(options).Apply(modeled, {0.25});
    EXPECT_GT(*std::max_element(modeled.Values().begin(), modeled.Values().end()), 1.0f)
        << "models stand in fillets too";
}

TEST(BoulderLayer, SetsBurialByWearWhenAsked) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.ejecta.minCraterDiameter = 50.0;
    options.ejecta.coverage = 0.15;
    options.pose.burialByWear = 1.0;
    const auto craters = Craters({{0.0, 0.0, 100.0, 0.0, 0u}});
    const BoulderLayer layer(options, craters);
    EXPECT_EQ(layer.AbundanceAt(50.0, 0.0).ejectaWear, 0.0) << "a fresh crater";
    std::size_t fresh = 0;
    const auto nearRim = terraingen::testing::Records(layer.Boulders({40.0, -10.0, 60.0, 10.0}));
    for (const Boulder& boulder : nearRim) {
        fresh += boulder.burial == 0.1 ? 1u : 0u;
    }
    EXPECT_GT(fresh, nearRim.size() / 2) << "most blocks at the rim are the fresh crater's";
    double least = 1.0;
    double most = 0.0;
    for (const Boulder& boulder :
         terraingen::testing::Records(layer.Boulders({300.0, -10.0, 320.0, 10.0}))) {
        least = std::fmin(least, boulder.burial);
        most = std::fmax(most, boulder.burial);
    }
    EXPECT_LT(least, 0.15) << "boulders between craters span every stage of wear";
    EXPECT_GT(most, 0.35);
    options.pose.burialByWear = 1.5;
    EXPECT_THROW(BoulderLayer(options, craters), std::invalid_argument);
}

TEST(BoulderModelThreshold, KeepsABandOfSizesAsModels) {
    terraingen::BoulderModelThreshold band;
    band.minDiameter = 1.0;
    band.maxDiameter = 3.0;
    EXPECT_FALSE(band.IsModel(0.5, 0.0));
    EXPECT_TRUE(band.IsModel(1.0, 0.0));
    EXPECT_FALSE(band.IsModel(3.0, 0.0));
    band = terraingen::BoulderModelThreshold{};
    band.minPixels = 4.0;
    band.maxPixels = 10.0;
    EXPECT_TRUE(band.IsModel(1.0, 0.2));
    EXPECT_FALSE(band.IsModel(2.0, 0.2));
    EXPECT_FALSE(band.IsModel(2.0, 0.0)) << "no samples without a spacing";
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.rendering.modelThreshold.maxDiameter = -1.0;
    EXPECT_THROW(BoulderLayer{options}, std::invalid_argument);
}

TEST(BoulderLayer, DrawsOnlyBouldersInItsReliefRange) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.1;
    options.sizes.minDiameter = 0.2;
    options.rendering.reliefRange = {0.6, 1.5};
    const BoulderLayer layer(options);
    Raster<float> heights(RasterGrid{120, 120, GeoTransform::NorthUp(-15.0, 15.0, 0.25)}, 0.0f);
    layer.Apply(heights, {0.25});
    const auto boulders =
        terraingen::testing::Records(layer.Boulders(heights.Grid().Bounds(), 0.25));
    std::size_t drawn = 0;
    for (const Boulder& boulder : boulders) {
        const auto pixel = heights.Transform().WorldToPixel(boulder.x, boulder.y);
        if (pixel.x < 0.0 || pixel.y < 0.0 || pixel.x >= 120.0 || pixel.y >= 120.0) {
            continue;
        }
        const float at =
            heights(static_cast<std::size_t>(pixel.x), static_cast<std::size_t>(pixel.y));
        const bool inRange = boulder.diameter >= 0.6 && boulder.diameter < 1.5;
        if (!inRange) {
            bool covered = false;
            for (const Boulder& other : boulders) {
                covered = covered || (other.diameter >= 0.6 && other.diameter < 1.5 &&
                                      std::hypot(other.x - boulder.x, other.y - boulder.y) <
                                          0.5 * other.diameter + 0.25);
            }
            if (!covered) {
                EXPECT_EQ(at, 0.0f) << "a " << boulder.diameter << " m boulder is not drawn";
            }
        } else {
            drawn += at > 0.0f ? 1u : 0u;
        }
    }
    EXPECT_GT(drawn, 0u);
    EXPECT_FALSE(options.rendering.reliefRange.Holds(1.0, true)) << "not models by default";
    options.rendering.reliefRange = {1.0, 0.5};
    EXPECT_THROW(BoulderLayer{options}, std::invalid_argument);
}

TEST(BoulderLayer, DrawsNoBoulderNarrowerThanTheFadeStartButListsEveryOne) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.1;
    options.sizes.minDiameter = 0.2;
    options.sizes.maxDiameter = 3.0;
    options.fade = {3.0, 5.0};
    const BoulderLayer layer(options);
    constexpr double kSpacing = 0.25;
    const double smallest = options.fade.startSamples * kSpacing;
    const terraingen::RasterGrid grid{100, 80, GeoTransform::NorthUp(-12.5, 10.0, kSpacing)};
    Raster<float> heights(grid, 0.0f);
    layer.Apply(heights, {kSpacing, 2});
    const std::vector<Boulder> listed =
        terraingen::testing::Records(layer.BouldersOver(grid, kSpacing, nullptr).boulders);
    std::size_t small = 0;
    std::size_t drawn = 0;
    for (const Boulder& boulder : listed) {
        const auto pixel = grid.transform.WorldToPixel(boulder.x, boulder.y);
        if (pixel.x < 0.0 || pixel.y < 0.0 || pixel.x >= 100.0 || pixel.y >= 80.0) {
            continue;
        }
        const float at =
            heights(static_cast<std::size_t>(pixel.x), static_cast<std::size_t>(pixel.y));
        if (boulder.diameter >= options.fade.endSamples * kSpacing) {
            drawn += at > 0.0f ? 1u : 0u;
            continue;
        }
        if (boulder.diameter >= smallest) {
            continue;
        }
        ++small;
        const bool covered = std::any_of(listed.begin(), listed.end(), [&](const Boulder& other) {
            return other.diameter >= smallest &&
                   std::hypot(other.x - boulder.x, other.y - boulder.y) <
                       0.5 * other.diameter + kSpacing;
        });
        if (!covered) {
            EXPECT_EQ(at, 0.0f) << "a " << boulder.diameter << " m boulder is drawn";
        }
    }
    EXPECT_GT(small, 10u) << "boulders too small for the heights are still listed";
    EXPECT_GT(drawn, 0u);
    for (const double x0 : {-12.5, 0.0}) {
        Raster<float> tile(RasterGrid{50, 80, GeoTransform::NorthUp(x0, 10.0, kSpacing)}, 0.0f);
        layer.Apply(tile, {kSpacing, 1});
        const auto offset = static_cast<std::size_t>((x0 + 12.5) / kSpacing);
        for (std::size_t row = 0; row < 80; ++row) {
            for (std::size_t col = 0; col < 50; ++col) {
                ASSERT_EQ(tile(col, row), heights(offset + col, row));
                const auto center = tile.CellCenter(col, row);
                ASSERT_EQ(layer.ApplyAt(0.0f, center.x, center.y, {kSpacing}), tile(col, row));
            }
        }
    }
}

TEST(BoulderLayer, DrawsOverlappingBouldersAsTheirUnion) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.3;
    options.sizes.minDiameter = 0.5;
    const BoulderLayer layer(options);
    Raster<float> heights(RasterGrid{160, 160, GeoTransform::NorthUp(-20.0, 20.0, 0.25)}, 0.0f);
    layer.Apply(heights, {0.25});
    double tallest = 0.0;
    for (const Boulder& boulder :
         terraingen::testing::Records(layer.Boulders(heights.Grid().Bounds(), 0.25))) {
        tallest = std::fmax(tallest, boulder.ExposedHeight());
    }
    const float highest = *std::max_element(heights.Values().begin(), heights.Values().end());
    EXPECT_LE(highest, tallest + 1e-5) << "no boulder stacks on another";
}

TEST(TiltRotation, TipsTheVerticalTowardItsAzimuth) {
    const auto turn = terraingen::TiltRotation(0.5, 0.25 * terraingen::kPi);
    const terraingen::Vec3d up = turn.Apply({0.0, 0.0, 1.0});
    EXPECT_NEAR(up.z, std::cos(0.5), 1e-12);
    EXPECT_NEAR(up.x, std::sin(0.5) * std::sqrt(0.5), 1e-12);
    EXPECT_NEAR(up.y, std::sin(0.5) * std::sqrt(0.5), 1e-12);
    const terraingen::Vec3d axis = turn.Apply({-std::sqrt(0.5), std::sqrt(0.5), 0.0});
    EXPECT_NEAR(axis.x, -std::sqrt(0.5), 1e-12) << "the axis stays put";
}

TEST(BoulderLayer, RestsBouldersOnSidesAndEndsAndTiltsThem) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.08;
    options.pose.sideShare = 0.3;
    options.pose.endShare = 0.1;
    options.pose.maxTiltDegrees = 0.8 * terraingen::kRadian;
    const BoulderLayer layer(options);
    const auto boulders = terraingen::testing::Records(layer.Boulders({-40.0, -40.0, 40.0, 40.0}));
    ASSERT_GT(boulders.size(), 500u);
    std::size_t sides = 0;
    std::size_t ends = 0;
    double steepest = 0.0;
    for (const Boulder& boulder : boulders) {
        sides += boulder.rest == terraingen::BoulderRest::Side ? 1u : 0u;
        ends += boulder.rest == terraingen::BoulderRest::End ? 1u : 0u;
        ASSERT_LE(boulder.tilt, 0.8 + 1e-12);
        steepest = std::fmax(steepest, boulder.tilt);
    }
    const double n = static_cast<double>(boulders.size());
    EXPECT_NEAR(static_cast<double>(sides) / n, 0.3, 0.05);
    EXPECT_NEAR(static_cast<double>(ends) / n, 0.1, 0.04);
    EXPECT_GT(steepest, 0.7);

    const auto shape = layer.Shape(boulders[0]);
    Boulder upright = boulders[0];
    upright.rest = terraingen::BoulderRest::End;
    upright.tilt = 0.0;
    upright.lean = 0.0;
    terraingen::TriangleMesh placed = shape->mesh;
    const auto placement = terraingen::PlaceBoulderModel(upright, *shape, 0.0, false);
    for (auto& p : placed.positions) {
        p = placement.Apply(p);
    }
    EXPECT_NEAR(terraingen::Bounds(placed).Extents().z, upright.diameter, 1e-9)
        << "standing on its end, its long axis is vertical";
    options.pose.sideShare = 0.95;
    EXPECT_THROW(BoulderLayer{options}, std::invalid_argument);
}

TEST(BoulderLayer, SettlesBouldersOnTheSlopeBeneathThem) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.08;
    options.pose.followSlope = true;
    const BoulderLayer layer(options);
    const Analytic ramp([](double x, double) { return static_cast<float>(0.5 * x); });
    for (const Boulder& boulder :
         terraingen::testing::Records(layer.Boulders({-10.0, -10.0, 10.0, 10.0}, 0.0, &ramp))) {
        ASSERT_NEAR(boulder.tilt, std::atan(0.5), 1e-4);
        ASSERT_NEAR(std::fabs(boulder.tiltAzimuth), terraingen::kPi, 1e-4) << "tipped downhill";
    }
}

TEST(BoulderLayer, LeavesOutBouldersOnGroundSteeperThanTheirMaxSlope) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.08;
    const Analytic gentle([](double x, double) { return static_cast<float>(0.2 * x); });
    const Analytic steep([](double x, double) { return static_cast<float>(0.8 * x); });
    const Analytic hill([](double x, double) { return static_cast<float>(0.05 * x * x); });
    const terraingen::Bounds2d region{-10.0, -10.0, 10.0, 10.0};
    const auto all =
        terraingen::testing::Records(BoulderLayer(options).Boulders(region, 0.0, &gentle));
    ASSERT_FALSE(all.empty());
    options.pose.maxSlopeDegrees = (std::atan(0.5)) * terraingen::kRadian;
    const BoulderLayer limited(options);
    EXPECT_EQ(terraingen::testing::Records(limited.Boulders(region, 0.0, &gentle)).size(),
              all.size());
    EXPECT_TRUE(terraingen::testing::Records(limited.Boulders(region, 0.0, &steep)).empty());
    const auto onHill = terraingen::testing::Records(limited.Boulders(region, 0.0, &hill));
    ASSERT_FALSE(onHill.empty());
    EXPECT_LT(onHill.size(), all.size());
    for (const Boulder& boulder : onHill) {
        ASSERT_LE(std::fabs(boulder.x), 5.0 + 1e-9) << "kept only where 0.1 |x| <= 0.5";
    }
    BoulderLayerOptions unlimited = options;
    unlimited.pose.maxSlopeDegrees.reset();
    EXPECT_EQ(
        terraingen::testing::Records(limited.Boulders(region, 0.0, nullptr)).size(),
        terraingen::testing::Records(BoulderLayer(unlimited).Boulders(region, 0.0, nullptr)).size())
        << "without the ground nothing is left out";
    options.pose.maxSlopeDegrees = 0.0 * terraingen::kRadian;
    EXPECT_THROW(BoulderLayer{options}, std::invalid_argument);
    options.pose.maxSlopeDegrees = 1.6 * terraingen::kRadian;
    EXPECT_THROW(BoulderLayer{options}, std::invalid_argument);
}

TEST(BoulderLayer, DrawsTiltedShapesThatPointQueriesMatch) {
    for (const std::uint32_t seed : {1u, 2u, 3u, 4u}) {
        BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
        options.seed = seed;
        options.sizes.coverage = 0.1;
        options.sizes.minDiameter = 0.5;
        options.pose.sideShare = 0.4;
        options.pose.maxTiltDegrees = 0.6 * terraingen::kRadian;
        options.rendering.relief = terraingen::BoulderRelief::Shape;
        options.rendering.shapes.subdivisions = 1;
        const BoulderLayer layer(options);
        Raster<float> heights(RasterGrid{60, 60, GeoTransform::NorthUp(-7.5, 7.5, 0.25)}, 1.0f);
        layer.Apply(heights, {0.25, 4});
        for (std::size_t row = 0; row < heights.Height(); ++row) {
            for (std::size_t col = 0; col < heights.Width(); ++col) {
                const auto center = heights.CellCenter(col, row);
                ASSERT_EQ(layer.ApplyAt(1.0f, center.x, center.y, {0.25}), heights(col, row))
                    << "seed " << seed;
            }
        }
    }
}

TEST(BoulderLayer, ReportsRockWhereBoulderTopsStandAndNotOnFillets) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.05;
    options.sizes.minDiameter = 1.0;
    options.sizes.maxDiameter = 2.0;
    options.fillet.heightShare = 0.5;
    options.fillet.widthRatio = 0.3;
    const BoulderLayer layer(options);
    EXPECT_EQ(layer.Material(), terraingen::MaterialClass::Rock);
    const auto boulders = terraingen::testing::Records(layer.Boulders({-50.0, -50.0, 50.0, 50.0}));
    ASSERT_FALSE(boulders.empty());
    const Boulder& b = boulders.front();
    EXPECT_EQ(layer.MaterialCoverAt(b.x, b.y, {0.05}), 1.0);
    const double onFillet = 0.5 * b.diameter + 0.15 * b.diameter;
    double fillet = 1.0;
    for (int k = 0; k < 8; ++k) {
        const double turn = 0.25 * terraingen::kPi * k;
        fillet = std::min(fillet, layer.MaterialCoverAt(b.x + onFillet * std::cos(turn),
                                                        b.y + onFillet * std::sin(turn), {0.05}));
    }
    EXPECT_EQ(fillet, 0.0);
    EXPECT_EQ(layer.MaterialCoverAt(b.x, b.y, {10.0}), 0.0) << "unresolved";
    EXPECT_EQ(layer.MaterialCoverAt(1e5, 1e5, {0.05}), 0.0);
}

TEST(BoulderLayer, ReportsRockOverShapesAndModelFootprints) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.05;
    options.sizes.minDiameter = 1.0;
    options.sizes.maxDiameter = 2.0;
    options.rendering.relief = terraingen::BoulderRelief::Shape;
    const BoulderLayer shapes(options);
    const Boulder b =
        terraingen::testing::Records(shapes.Boulders({-50.0, -50.0, 50.0, 50.0})).front();
    EXPECT_EQ(shapes.MaterialCoverAt(b.x, b.y, {0.05}), 1.0);
    options.rendering.modelThreshold.minDiameter = 1.0;
    const BoulderLayer models(options);
    const Boulder m =
        terraingen::testing::Records(models.Boulders({-50.0, -50.0, 50.0, 50.0})).front();
    ASSERT_TRUE(m.model);
    EXPECT_EQ(models.MaterialCoverAt(m.x, m.y, {0.05}), 1.0);
    EXPECT_EQ(models.MaterialCoverAt(m.x + 0.51 * m.diameter / std::sqrt(0.5),
                                     m.y + 0.51 * m.diameter / std::sqrt(0.5), {0.05}),
              0.0);
}

TEST(BoulderLayer, GivesTheSameRockCoverOverAGridAsAtItsPoints) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.1;
    options.sizes.minDiameter = 0.2;
    options.sizes.maxDiameter = 2.0;
    options.rendering.modelThreshold.minDiameter = 0.8;
    const BoulderLayer layer(options);
    const terraingen::RasterGrid grid{160, 120, GeoTransform::NorthUp(-16.0, 12.0, 0.2)};
    const auto boulders = terraingen::testing::Records(layer.Boulders(grid.Bounds(), 0.2));
    ASSERT_TRUE(
        std::any_of(boulders.begin(), boulders.end(), [](const Boulder& b) { return b.model; }));
    const Raster<double> cover = layer.MaterialCoverOver(grid, {0.2, 3});
    double rock = 0.0;
    for (std::size_t row = 0; row < grid.height; ++row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            const auto center = cover.CellCenter(col, row);
            ASSERT_EQ(cover(col, row), layer.MaterialCoverAt(center.x, center.y, {0.2}));
            rock += cover(col, row);
        }
    }
    EXPECT_GT(rock, 100.0);
}

TEST(BoulderLayer, FacetsTheirShareOfBouldersUpwindAndKeelsSome) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.1;
    options.ventifacts.share = 0.4;
    options.ventifacts.windAzimuthDegrees = 0.3 * terraingen::kRadian;
    options.ventifacts.secondWindAzimuthDegrees = 2.5 * terraingen::kRadian;
    options.ventifacts.keelShare = 0.5;
    const BoulderLayer layer(options);
    const auto boulders = terraingen::testing::Records(layer.Boulders({-20.0, -20.0, 20.0, 20.0}));
    ASSERT_GT(boulders.size(), 500u);
    std::size_t faceted = 0;
    std::size_t keeled = 0;
    for (const Boulder& b : boulders) {
        if (b.facetCount == 0) {
            continue;
        }
        ++faceted;
        keeled += b.facetCount == 2 ? 1 : 0;
        EXPECT_NEAR(b.facets[0].azimuth, 0.3 + terraingen::kPi, 1e-12) << "facing upwind";
        EXPECT_GE(b.facets[0].angle, (options.ventifacts.minAngleDegrees * terraingen::kDegree));
        EXPECT_LE(b.facets[0].angle, (options.ventifacts.maxAngleDegrees * terraingen::kDegree));
        EXPECT_NEAR(b.facets[0].reach, 0.5 * b.diameter * (1.0 - b.wear), 1e-12);
        if (b.facetCount == 2) {
            EXPECT_NEAR(b.facets[1].azimuth, 2.5 + terraingen::kPi, 1e-12);
        }
    }
    const double share = static_cast<double>(faceted) / static_cast<double>(boulders.size());
    EXPECT_NEAR(share, 0.4, 0.05);
    EXPECT_NEAR(static_cast<double>(keeled) / static_cast<double>(faceted), 0.5, 0.08);
    options.ventifacts.share = 0.0;
    const auto plain =
        terraingen::testing::Records(BoulderLayer(options).Boulders({-20.0, -20.0, 20.0, 20.0}));
    ASSERT_EQ(plain.size(), boulders.size());
    for (std::size_t k = 0; k < plain.size(); ++k) {
        EXPECT_EQ(plain[k].facetCount, 0);
        Boulder same = boulders[k];
        same.facetCount = 0;
        same.facets = {};
        EXPECT_EQ(plain[k], same) << "facets change nothing else";
    }
}

TEST(BoulderLayer, FacetsCutTheWindwardSideOfDomesShapesAndModels) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.05;
    options.sizes.minDiameter = 1.0;
    options.sizes.maxDiameter = 2.0;
    options.ventifacts.share = 1.0;
    options.ventifacts.windAzimuthDegrees = 0.0 * terraingen::kRadian;
    BoulderLayerOptions plainOptions = options;
    plainOptions.ventifacts.share = 0.0;
    for (const auto relief : {terraingen::BoulderRelief::Dome, terraingen::BoulderRelief::Shape}) {
        options.rendering.relief = relief;
        plainOptions.rendering.relief = relief;
        const BoulderLayer cut(options);
        const BoulderLayer plain(plainOptions);
        const Boulder b =
            terraingen::testing::Records(cut.Boulders({-50.0, -50.0, 50.0, 50.0})).front();
        ASSERT_EQ(b.facetCount, 1);
        double lowered = 0.0;
        for (double t = -0.45; t <= 0.45; t += 0.05) {
            const double x = b.x + t * b.diameter;
            const double h = static_cast<double>(cut.ApplyAt(0.0f, x, b.y, {0.01}));
            const double before = static_cast<double>(plain.ApplyAt(0.0f, x, b.y, {0.01}));
            EXPECT_LE(h, before + 1e-6);
            EXPECT_LE(h, std::fmax(b.facets[0].HeightAt({x - b.x, 0.0}), 0.0) + 1e-6);
            lowered = std::fmax(lowered, before - h);
            if (t > 0.2) {
                EXPECT_NEAR(h, before, 1e-6) << "the lee side keeps its rock";
            }
        }
        EXPECT_GT(lowered, 0.01) << "the windward side is planed";
    }
    options.rendering.modelThreshold.minDiameter = 1.0;
    const BoulderLayer models(options);
    const Analytic flat([](double, double) { return 2.0f; });
    const auto placed = models.BouldersOver(
        terraingen::RasterGrid{400, 400, GeoTransform::NorthUp(-20.0, 20.0, 0.1)}, 0.1, &flat);
    ASSERT_FALSE(placed.models.empty());
    for (const auto& model : placed.models) {
        const Boulder& b = model.boulder;
        ASSERT_EQ(b.facetCount, 1);
        for (const auto& q : model.shape->mesh.positions) {
            const auto p = model.placement.Apply(q);
            EXPECT_LE(p.z - 2.0, b.facets[0].HeightAt({p.x - b.x, p.y - b.y}) + 1e-9);
        }
        std::map<std::pair<std::uint32_t, std::uint32_t>, int> edges;
        for (const auto& t : model.shape->mesh.triangles) {
            for (std::size_t k = 0; k < 3; ++k) {
                ++edges[std::minmax(t[k], t[(k + 1) % 3])];
            }
        }
        for (const auto& [edge, uses] : edges) {
            ASSERT_EQ(uses, 2) << "the cut mesh stays closed";
        }
    }
}

TEST(BoulderLayer, BrokenPiecesLoseTheirParentsFacets) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.05;
    options.sizes.minDiameter = 1.0;
    options.sizes.maxDiameter = 3.0;
    options.fragmentation.emplace();
    options.fragmentation->splitWear = 0.0;
    options.ventifacts.share = 1.0;
    const BoulderLayer layer(options);
    const auto boulders = terraingen::testing::Records(layer.Boulders({-30.0, -30.0, 30.0, 30.0}));
    ASSERT_TRUE(std::any_of(boulders.begin(), boulders.end(),
                            [](const Boulder& b) { return b.parent != 0; }));
    for (const Boulder& b : boulders) {
        EXPECT_EQ(b.facetCount, b.parent != 0 ? 0 : 1);
    }
}

TEST(BoulderVentifacts, GaleSharesAndRejectsBadOptions) {
    const terraingen::BoulderVentifacts gale = terraingen::GaleVentifacts();
    EXPECT_DOUBLE_EQ(gale.share, 0.39);
    EXPECT_DOUBLE_EQ(gale.keelShare, 12.0 / 39.0);
    EXPECT_NO_THROW(terraingen::ValidateBoulderVentifacts(gale));
    const auto rejects = [](auto change) {
        terraingen::BoulderVentifacts ventifacts;
        change(ventifacts);
        EXPECT_THROW(terraingen::ValidateBoulderVentifacts(ventifacts), std::invalid_argument);
    };
    rejects([](auto& v) { v.share = 1.5; });
    rejects([](auto& v) { v.keelShare = -0.1; });
    rejects([](auto& v) { v.minAngleDegrees = 0.0 * terraingen::kRadian; });
    rejects([](auto& v) { v.maxAngleDegrees = (0.5 * terraingen::kPi) * terraingen::kRadian; });
    rejects([](auto& v) {
        v.minAngleDegrees = 1.0 * terraingen::kRadian;
        v.maxAngleDegrees = 0.9 * terraingen::kRadian;
    });
    rejects([](auto& v) {
        v.windAzimuthDegrees = (std::numeric_limits<double>::quiet_NaN()) * terraingen::kRadian;
    });
    rejects([](auto& v) {
        v.secondWindAzimuthDegrees =
            (std::numeric_limits<double>::infinity()) * terraingen::kRadian;
    });
    Boulder b;
    b.facets[0] = {0.0, terraingen::kPi / 4.0, 0.5};
    b.facetCount = 1;
    EXPECT_NEAR(b.facets[0].HeightAt({0.2, 7.0}), 0.3, 1e-12);
    EXPECT_NEAR(b.CutByFacets({0.2, 0.0}, 1.0), 0.3, 1e-12);
    EXPECT_EQ(b.CutByFacets({0.9, 0.0}, 1.0), 0.0);
    EXPECT_EQ(b.CutByFacets({-0.9, 0.0}, 1.0), 1.0);
}

TEST(BoulderMapAbundance, ScalesTheSizesToTheMapLessTheCraterBlocks) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.distribution = std::make_shared<terraingen::PowerLawSfd>(0.01, 2.5);
    options.ejecta.minCraterDiameter = 10.0;
    options.ejecta.distribution = std::make_shared<terraingen::PowerLawSfd>(0.02, 2.5);
    terraingen::DensityMap map{std::make_shared<terraingen::ConstantField>(3.0),
                               terraingen::DensityMeasure::Count, 4.5};
    map.areaUnit = 1e6;
    options.densityMap = map;
    const double measure = 0.01 * std::pow(4.5, -2.5);
    const double target = 3.0 / 1e6;
    const terraingen::BoulderMapAbundance abundance(options);
    EXPECT_DOUBLE_EQ(abundance.At(target, 0.0), target / measure);
    EXPECT_DOUBLE_EQ(abundance.At(target, 0.001), (target - 0.001 * 2.0 * measure) / measure)
        << "the crater blocks are taken out of a total map";
    EXPECT_EQ(abundance.At(target, 1.0), 0.0) << "blocks alone make up the map";
    EXPECT_EQ(abundance.At(std::nan(""), 0.5), 1.0);
    EXPECT_DOUBLE_EQ(abundance.Max(), std::fmax(1.0, target / measure));
    options.densityMap->total = false;
    EXPECT_DOUBLE_EQ(terraingen::BoulderMapAbundance(options).At(target, 0.001), target / measure);
    options.densityMap = std::nullopt;
    EXPECT_THROW(terraingen::BoulderMapAbundance{options}, std::invalid_argument);
}

TEST(BoulderMapAbundance, SolvesGolombekRappCoverageForRockAbundance) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.05;
    options.densityMap = terraingen::DensityMap{std::make_shared<terraingen::ConstantField>(0.02),
                                                terraingen::DensityMeasure::ArealFraction, 1.0};
    const terraingen::BoulderMapAbundance abundance(options);
    for (const double fraction : {1e-5, 0.001, 0.02, 0.1}) {
        const double k = abundance.At(fraction, 0.0) * 0.05;
        EXPECT_NEAR(terraingen::GolombekRappSfd(k).FractionalArea(1.0), fraction, 2e-3 * fraction)
            << fraction;
    }
    EXPECT_GT(abundance.At(1e-9, 0.0), 0.0);
    EXPECT_LT(abundance.At(1e-9, 0.0), abundance.At(1e-5, 0.0)) << "rising with the fraction";
    EXPECT_DOUBLE_EQ(abundance.At(0.9, 0.0) * 0.05, 0.999) << "capped below full cover";
    options.densityMap->minDiameter = 0.001;
    const terraingen::BoulderMapAbundance fine(options);
    EXPECT_NEAR(fine.At(1e-80, 0.0) / fine.At(2e-80, 0.0), 0.5, 1e-9)
        << "proportional below the table";
    options.densityMap->minDiameter = 1.0;
    options.densityMap->measure = terraingen::DensityMeasure::Count;
    options.densityMap->maxDiameter = 3.0;
    const double count = terraingen::GolombekRappSfd(0.03).ExpectedCount(1.0, 1.0, 3.0);
    EXPECT_NEAR(terraingen::BoulderMapAbundance(options).At(count, 0.0) * 0.05, 0.03, 1e-4);
}

TEST(BoulderLayer, FollowsADensityMapBetweenCraters) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.02;
    options.densityMap = terraingen::DensityMap{std::make_shared<terraingen::ConstantField>(2.5)};
    const BoulderLayer layer(options);
    EXPECT_DOUBLE_EQ(layer.AbundanceAt(3.0, 4.0).background, 2.5);
    BoulderLayerOptions plain = terraingen::testing::GolombekRappBoulders();
    plain.sizes.coverage = 0.02;
    const Bounds2d region{-15.0, -15.0, 15.0, 15.0};
    const double ratio =
        static_cast<double>(terraingen::testing::Records(layer.Boulders(region, 0.0)).size()) /
        static_cast<double>(
            terraingen::testing::Records(BoulderLayer(plain).Boulders(region, 0.0)).size());
    EXPECT_GT(ratio, 1.8) << "a higher coverage gives more boulders";
    options.sizes.coverage = 0.5;
    EXPECT_THROW(BoulderLayer{options}, std::invalid_argument) << "coverage over 1 under the map";
}

TEST(BoulderLayer, PlacesCatalogBouldersAndDropsGeneratedOnesItCovers) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.08;
    options.sizes.minDiameter = 0.3;
    options.manual = {{2.0, 3.0, 12.0}, {-4.0, 1.0, 0.8}};
    options.completeness = std::make_shared<terraingen::ConstantField>(0.6);
    options.pose.maxSlopeDegrees = 0.1 * terraingen::kRadian;
    const Analytic steep([](double x, double) { return static_cast<float>(0.5 * x); });
    const Bounds2d region{-10.0, -10.0, 10.0, 10.0};
    const auto boulders =
        terraingen::testing::Records(BoulderLayer(options).Boulders(region, 0.0, &steep));
    ASSERT_EQ(boulders.size(), 2u) << "steep ground leaves out every generated boulder";
    EXPECT_EQ(boulders[0].diameter, 12.0) << "beyond the sizes, still placed";
    options.pose.maxSlopeDegrees.reset();
    std::size_t manual = 0;
    std::size_t small = 0;
    for (const Boulder& boulder :
         terraingen::testing::Records(BoulderLayer(options).Boulders(region, 0.0))) {
        if (boulder.diameter == 12.0 || boulder.diameter == 0.8) {
            ++manual;
            continue;
        }
        ASSERT_LT(boulder.diameter, 0.6) << "the catalog stands in from 0.6 m";
        ++small;
    }
    EXPECT_EQ(manual, 2u);
    EXPECT_GT(small, 0u);
}

TEST(BoulderLayer, KeepsAMapsMeanUnderPatchesAndBedrock) {
    const auto mean = [](const BoulderLayer& layer, const terraingen::Ground* ground, double size) {
        double sum = 0.0;
        int count = 0;
        for (double y = 1.0; y < size; y += 2.0) {
            for (double x = 1.0; x < size; x += 2.0) {
                sum += layer.AbundanceAt(x, y, ground).background;
                ++count;
            }
        }
        return sum / count;
    };
    BoulderLayerOptions patched = terraingen::testing::GolombekRappBoulders();
    patched.sizes.coverage = 0.01;
    patched.patches = {4.0, 20.0};
    const double raised = terraingen::MeanPatchAbundance(patched.patches);
    EXPECT_GT(raised, 1.2) << "patches alone raise the mean";
    EXPECT_EQ(terraingen::MeanPatchAbundance({}), 1.0);
    EXPECT_NEAR(mean(BoulderLayer(patched), nullptr, 2000.0), raised, 0.05 * raised);
    patched.densityMap = terraingen::DensityMap{std::make_shared<terraingen::ConstantField>(1.0)};
    EXPECT_NEAR(mean(BoulderLayer(patched), nullptr, 2000.0), 1.0, 0.05);

    BoulderLayerOptions bedrock = terraingen::testing::GolombekRappBoulders();
    bedrock.sizes.coverage = 0.01;
    bedrock.bedrock.exposure = terraingen::ExposureParams{};
    bedrock.bedrock.meanSize = 100.0;
    const Analytic steps([](double x, double) {
        const double within = x - 100.0 * std::floor(x / 100.0);
        return static_cast<float>(0.8 * std::fmax(0.0, within - 50.0) +
                                  40.0 * std::floor(x / 100.0));
    });
    const double unheld = mean(BoulderLayer(bedrock), &steps, 400.0);
    EXPECT_GT(unheld, 1.5) << "bedrock alone raises the mean";
    bedrock.densityMap = terraingen::DensityMap{std::make_shared<terraingen::ConstantField>(1.0)};
    const BoulderLayer held(bedrock);
    EXPECT_NEAR(mean(held, &steps, 400.0), 1.0, 0.05);
    EXPECT_GT(held.AbundanceAt(80.0, 50.0, &steps).background,
              2.0 * held.AbundanceAt(20.0, 50.0, &steps).background)
        << "still gathered on the bedrock";
    Raster<float> cells(RasterGrid{4, 1, GeoTransform::NorthUp(0.0, 100.0, 100.0)}, 1.0f);
    bedrock.bedrock.meanSize = 1e6;
    bedrock.densityMap->field = std::make_shared<terraingen::RasterField>(cells);
    EXPECT_NEAR(mean(BoulderLayer(bedrock), &steps, 400.0), 1.0, 0.05) << "over the map's cells";
}

namespace {

/// A field that is `west` for negative x and `east` otherwise.
class WestEast final : public terraingen::SurfaceField {
  public:
    WestEast(double west, double east) : west_(west), east_(east) {}
    double ValueAt(double x, double /*y*/) const override {
        return x < 0.0 ? west_ : east_;
    }
    double Max() const override {
        return std::fmax(west_, east_);
    }

  private:
    double west_;
    double east_;
};

}  // namespace

TEST(BoulderLayer, BendsTheSizesIntoTheSlopeAMapGives) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.distribution = std::make_shared<terraingen::PowerLawSfd>(0.05, 3.0);
    options.sizes.minDiameter = 0.5;
    options.sizes.maxDiameter = 8.0;
    options.slopeMap = terraingen::SizeSlopeMap{std::make_shared<WestEast>(2.0, 4.0)};
    options.slopeMap->referenceDiameter = 1.0;
    options.slopeMap->minExponent = 1.5;
    options.slopeMap->maxExponent = 5.0;
    const BoulderLayer layer(options);
    EXPECT_EQ(*layer.AbundanceAt(-5.0, 0.0).exponent, 2.0);
    for (const double d : {0.5, 1.0, 3.0, 7.0}) {
        const auto candidates = terraingen::BoulderCandidates(options);
        for (const double alpha : {1.5, 2.7, 5.0}) {
            const double local = 0.05 * alpha * std::pow(d, -alpha - 1.0);
            EXPECT_LE(local, candidates->Incremental(d) * (1.0 + 1e-12)) << d << " " << alpha;
        }
    }
    const Bounds2d region{-120.0, -60.0, 120.0, 60.0};
    double westSmall = 0.0;
    double westLarge = 0.0;
    double eastSmall = 0.0;
    double eastLarge = 0.0;
    for (const Boulder& boulder : terraingen::testing::Records(layer.Boulders(region, 0.0))) {
        const bool west = boulder.x < 0.0;
        (west ? westSmall : eastSmall) += boulder.diameter >= 1.0 ? 1.0 : 0.0;
        (west ? westLarge : eastLarge) += boulder.diameter >= 2.0 ? 1.0 : 0.0;
    }
    const double expected = 0.05 * 120.0 * 120.0;
    EXPECT_NEAR(westSmall, expected, 4.0 * std::sqrt(expected)) << "the count at D0 is kept";
    EXPECT_NEAR(eastSmall, expected, 4.0 * std::sqrt(expected));
    EXPECT_NEAR(westLarge / westSmall, 0.25, 0.03);
    EXPECT_NEAR(eastLarge / eastSmall, 1.0 / 16.0, 0.025);
    options.sizes.distribution = nullptr;
    EXPECT_THROW(BoulderLayer{options}, std::invalid_argument) << "not with Golombek-Rapp sizes";
}

TEST(BoulderLayer, RollsBouldersDownSteepGroundToRestAtItsFoot) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.08;
    options.pose.maxSlopeDegrees = (std::atan(0.5)) * terraingen::kRadian;
    options.pose.rollDistance = 40.0;
    const Analytic ramp(
        [](double x, double) { return static_cast<float>(0.8 * std::clamp(x, 0.0, 20.0)); });
    const Bounds2d region{-30.0, -10.0, 30.0, 10.0};
    BoulderLayerOptions still = options;
    still.pose.maxSlopeDegrees.reset();
    still.pose.rollDistance.reset();
    const auto unrolled =
        terraingen::testing::Records(BoulderLayer(still).Boulders(region, 0.0, &ramp));
    const BoulderLayer layer(options);
    const auto rolled = terraingen::testing::Records(layer.Boulders(region, 0.0, &ramp));
    std::size_t onRamp = 0;
    std::size_t atFoot = 0;
    std::size_t atFootBefore = 0;
    for (const Boulder& boulder : rolled) {
        onRamp += boulder.x > 1.0 && boulder.x < 19.0 ? 1u : 0u;
        atFoot += boulder.x > -3.0 && boulder.x < 1.0 ? 1u : 0u;
    }
    for (const Boulder& boulder : unrolled) {
        atFootBefore += boulder.x > -3.0 && boulder.x < 1.0 ? 1u : 0u;
    }
    EXPECT_EQ(onRamp, 0u) << "none rest on the steep ramp";
    EXPECT_GT(atFoot, 2 * atFootBefore) << "they gather at its foot";
    EXPECT_EQ(rolled.size(), unrolled.size()) << "within reach, none are lost";
    std::set<std::uint64_t> whole;
    for (const Boulder& boulder : rolled) {
        whole.insert(boulder.id);
    }
    std::set<std::uint64_t> tiles;
    for (const Bounds2d& tile :
         {Bounds2d{-30.0, -10.0, 0.5, 10.0}, Bounds2d{0.5, -10.0, 30.0, 10.0}}) {
        for (const Boulder& boulder :
             terraingen::testing::Records(layer.Boulders(tile, 0.0, &ramp))) {
            if (tile.Contains(boulder.x, boulder.y)) {
                tiles.insert(boulder.id);
            }
        }
    }
    std::set<std::uint64_t> inside;
    for (const Boulder& boulder : rolled) {
        if (region.Contains(boulder.x, boulder.y)) {
            inside.insert(boulder.id);
        }
    }
    EXPECT_EQ(tiles, inside) << "boulders rolling across a tile edge land in the right tile";
    options.pose.rollDistance = 3.0;
    EXPECT_LT(
        terraingen::testing::Records(BoulderLayer(options).Boulders(region, 0.0, &ramp)).size(),
        rolled.size())
        << "those that would roll farther are left out";
    options.pose.maxSlopeDegrees.reset();
    EXPECT_THROW(BoulderLayer{options}, std::invalid_argument) << "rolling needs a max slope";
}

namespace {

/// A sloping ground that counts how often it is read, and records any thread count but 1.
class Counted final : public terraingen::Ground {
  public:
    mutable std::atomic<int> reads_{0};
    mutable std::atomic<unsigned> threads_{1};

  protected:
    void FillSamples(terraingen::SampleBlock& samples, double /*spacing*/,
                     unsigned threads) const override {
        ++reads_;
        if (threads != 1) {
            threads_ = threads;
        }
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            const terraingen::Vec2d p = samples.Position(i);
            samples.Height(i) = static_cast<float>(0.1 * p.x + 0.05 * p.y * p.y);
        }
    }
};

}  // namespace

TEST(BoulderLayer, ReadsTheGroundBeneathItsBouldersInOnePass) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.05;
    options.pose.followSlope = true;
    options.pose.maxSlopeDegrees = 1.2 * terraingen::kRadian;
    const BoulderLayer layer(options);
    const Bounds2d region{-10.0, -10.0, 10.0, 10.0};
    const Counted counted;
    const auto boulders = terraingen::testing::Records(layer.Boulders(region, 0.0, &counted));
    ASSERT_GT(boulders.size(), 20u);
    EXPECT_EQ(counted.reads_.load(), 1) << "every point the boulders read was fetched at once";
}

TEST(BoulderLayer, ReadsTheGroundOnTheEvaluationsThreads) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.05;
    options.pose.followSlope = true;
    const BoulderLayer layer(options);
    const Counted counted;
    Raster<float> heights(RasterGrid{40, 40, GeoTransform::NorthUp(-10.0, 10.0, 0.5)}, 0.0f);
    terraingen::LayerContext context{0.5, 1};
    context.ground = &counted;
    layer.Apply(heights, context);
    ASSERT_GT(counted.reads_.load(), 0);
    EXPECT_EQ(counted.threads_.load(), 1u) << "a one-thread evaluation reads on one thread";
}

TEST(BoulderLayer, MeasuresUnresolvedCoverUpToItsLargestBoulder) {
    terraingen::BoulderLayerOptions options;
    options.seed = 3;
    const terraingen::BoulderLayer layer(options);
    for (const double spacing : {0.5, 1.0, 2.0, 2.93, 4.0, 10.0}) {
        const terraingen::UnresolvedCover cover = layer.UnresolvedCoverAt(0.0, 0.0, spacing);
        EXPECT_GE(cover.mean, 0.0) << spacing;
        EXPECT_GE(cover.variance, 0.0) << spacing;
    }
    EXPECT_GT(layer.UnresolvedCoverAt(0.0, 0.0, 2.0).mean, 0.0);
}
