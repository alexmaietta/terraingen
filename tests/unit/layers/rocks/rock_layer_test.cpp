#include "terraingen/layers/rocks/rock_layer.h"

#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>

using terraingen::Bounds2d;
using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::Rock;
using terraingen::RockLayer;
using terraingen::RockLayerOptions;

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
                                     std::make_shared<terraingen::UniformAges>(1.0)),
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

TEST(RockLayer, GivesEachRockStableAttributesInTheirRanges) {
    RockLayerOptions options;
    options.coverage = 0.05;
    const RockLayer layer(options);
    const auto rocks = layer.Rocks({-20.0, -20.0, 20.0, 20.0});
    ASSERT_GT(rocks.size(), 50u);
    std::set<std::uint64_t> ids;
    for (const Rock& rock : rocks) {
        EXPECT_GE(rock.diameter, 0.1);
        EXPECT_LT(rock.diameter, 4.0);
        EXPECT_GE(rock.height / rock.diameter, 0.4 - 1e-12);
        EXPECT_LE(rock.height / rock.diameter, 0.6 + 1e-12);
        EXPECT_GE(rock.burial, 0.1);
        EXPECT_LE(rock.burial, 0.4);
        EXPECT_GE(rock.yaw, 0.0);
        EXPECT_LT(rock.yaw, 2.0 * std::numbers::pi);
        EXPECT_LE(rock.lean, 0.26);
        EXPECT_DOUBLE_EQ(rock.ExposedHeight(), rock.height * (1.0 - rock.burial));
        ids.insert(rock.id);
    }
    EXPECT_EQ(ids.size(), rocks.size());
    EXPECT_EQ(layer.Rocks({-20.0, -20.0, 20.0, 20.0}), rocks);
}

TEST(RockLayer, GivesTheSameRocksInEveryRegionAndSkipsOnlyUnresolvedOnes) {
    RockLayerOptions options;
    options.coverage = 0.05;
    options.patches = {2.0, 30.0};
    const RockLayer layer(options);
    const auto whole = layer.Rocks({-30.0, -30.0, 30.0, 30.0});
    const auto part = layer.Rocks({-5.0, 0.0, 10.0, 12.0});
    std::set<std::uint64_t> wholeIds;
    for (const Rock& rock : whole) {
        wholeIds.insert(rock.id);
    }
    for (const Rock& rock : part) {
        EXPECT_TRUE(wholeIds.count(rock.id)) << rock.id;
    }
    const auto coarse = layer.Rocks({-30.0, -30.0, 30.0, 30.0}, 0.5);
    std::set<std::uint64_t> resolved;
    for (const Rock& rock : whole) {
        if (rock.diameter >= 2.0 * 0.5) {
            resolved.insert(rock.id);
        }
    }
    std::set<std::uint64_t> kept;
    for (const Rock& rock : coarse) {
        if (rock.diameter >= 1.0) {
            kept.insert(rock.id);
        }
    }
    EXPECT_EQ(kept, resolved);
    EXPECT_LT(coarse.size(), whole.size());
}

TEST(RockLayer, RaisesTheCoverToTheRimsOfFreshCraters) {
    RockLayerOptions options;
    options.ejecta = {0.15, 50.0, 3.0, 0.7};
    const RockLayer layer(options, Craters({{0.0, 0.0, 100.0, 0.0, 0u}}));
    EXPECT_DOUBLE_EQ(layer.CoverageAt(50.0, 0.0), 0.15) << "at the rim";
    EXPECT_DOUBLE_EQ(layer.CoverageAt(10.0, 5.0), 0.15) << "on the floor";
    const double halfway = layer.CoverageAt(100.0, 0.0);
    EXPECT_GT(halfway, 0.02);
    EXPECT_LT(halfway, 0.15);
    EXPECT_DOUBLE_EQ(layer.CoverageAt(160.0, 0.0), 0.02) << "beyond 3 radii";
    const auto nearRim = layer.Rocks({40.0, -10.0, 60.0, 10.0});
    const auto far = layer.Rocks({300.0, -10.0, 320.0, 10.0});
    EXPECT_GT(nearRim.size(), 3 * far.size());
    EXPECT_THROW(RockLayer{options}, std::invalid_argument) << "ejecta need craters";

    options.ejecta.minCraterDiameter = 200.0;
    EXPECT_DOUBLE_EQ(
        RockLayer(options, Craters({{0.0, 0.0, 100.0, 0.0, 0u}})).CoverageAt(50.0, 0.0), 0.02)
        << "smaller craters did not dig to rock";
}

TEST(RockLayer, RaisesTheCoverWhereBedrockShows) {
    RockLayerOptions options;
    options.patches.spread = 3.0;
    options.patches.size = 1e6;
    options.exposure = terraingen::ExposureParams{};
    const RockLayer layer(options);
    const Analytic steep([](double x, double) { return static_cast<float>(0.8 * x); });
    const Analytic level([](double, double) { return 0.0f; });
    const double soil = layer.CoverageAt(100.0, 0.0, &level);
    const double bedrock = layer.CoverageAt(100.0, 0.0, &steep);
    EXPECT_GT(bedrock, soil);
    EXPECT_NEAR(bedrock, soil + 0.5 * (0.06 - soil), 1e-12) << "halfway toward the richest cover";
    EXPECT_THROW(layer.CoverageAt(0.0, 0.0), std::invalid_argument);
    EXPECT_THROW(layer.Rocks({0.0, 0.0, 1.0, 1.0}), std::invalid_argument);
}

TEST(RockLayer, AddsDomesThatPointQueriesMatchToTheBit) {
    RockLayerOptions options;
    options.coverage = 0.1;
    options.minDiameter = 0.5;
    const RockLayer layer(options);
    Raster<float> heights(80, 60, 1.0f);
    heights.SetTransform(GeoTransform::NorthUp(-20.0, 15.0, 0.5));
    terraingen::TerrainFeatures features;
    layer.Apply(heights, {0.5, 4}, features);
    ASSERT_FALSE(features.rocks.empty());
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
    terraingen::TerrainFeatures again;
    layer.Apply(serial, {0.5, 1}, again);
    EXPECT_TRUE(
        std::equal(serial.Values().begin(), serial.Values().end(), heights.Values().begin()));
    EXPECT_EQ(again.rocks, features.rocks);
}

TEST(AddRock, IsADomeOfTheExposedHeight) {
    const Rock rock{0.0, 0.0, 2.0, 1.0, 0.25, 0.0, 0.0, 1u};
    EXPECT_FLOAT_EQ(terraingen::AddRock(5.0f, 0.0, 0.0, rock, 1.0), 5.75f);
    EXPECT_FLOAT_EQ(terraingen::AddRock(5.0f, 0.6, 0.0, rock, 1.0), 5.0f + 0.75f * 0.8f);
    EXPECT_FLOAT_EQ(terraingen::AddRock(5.0f, 0.0, 0.0, rock, 0.5), 5.375f);
    EXPECT_EQ(terraingen::AddRock(5.0f, 1.0, 0.0, rock, 1.0), 5.0f);
}

TEST(RockLayer, LeavesHeightsAloneWithoutReliefAndRejectsBadOptions) {
    RockLayerOptions options;
    options.coverage = 0.1;
    options.relief = terraingen::RockRelief::None;
    const RockLayer layer(options);
    Raster<float> heights(120, 120, 2.0f);
    heights.SetTransform(GeoTransform::NorthUp(0.0, 60.0, 0.5));
    terraingen::TerrainFeatures features;
    layer.Apply(heights, {0.5}, features);
    EXPECT_FALSE(features.rocks.empty());
    for (const float value : heights.Values()) {
        EXPECT_EQ(value, 2.0f);
    }
    EXPECT_EQ(layer.ApplyAt(2.0f, 1.0, 1.0, {0.5}), 2.0f);
    EXPECT_TRUE(std::isnan(RockLayer({}).ApplyAt(std::nanf(""), 1.0, 1.0, {0.5})));
    EXPECT_THROW(RockLayer({}).ApplyAt(0.0f, 1.0, 1.0, {0.0}), std::invalid_argument);
    EXPECT_EQ(layer.MaxFeatureDiameter(), 4.0);
    const std::vector<std::function<void(RockLayerOptions&)>> changes{
        [](RockLayerOptions& o) { o.coverage = 0.0; },
        [](RockLayerOptions& o) { o.maxDiameter = 0.05; },
        [](RockLayerOptions& o) { o.patches.spread = 0.5; },
        [](RockLayerOptions& o) { o.patches.spread = 60.0; },
        [](RockLayerOptions& o) { o.ejecta = {2.0, 10.0, 3.0, 0.7}; },
        [](RockLayerOptions& o) { o.ejecta.minCraterDiameter = -1.0; },
        [](RockLayerOptions& o) { o.exposureSpacing = 0.0; },
        [](RockLayerOptions& o) { o.exposure = terraingen::ExposureParams{.reach = 0.0}; },
        [](RockLayerOptions& o) { o.maxHeightRatio = 0.1; },
        [](RockLayerOptions& o) { o.maxBurial = 1.0; },
        [](RockLayerOptions& o) { o.maxLean = -1.0; },
        [](RockLayerOptions& o) { o.fade = {3.0, 3.0}; }};
    for (const auto& bad : changes) {
        RockLayerOptions invalid;
        bad(invalid);
        EXPECT_THROW(terraingen::ValidateRockLayerOptions(invalid), std::invalid_argument);
    }
}
