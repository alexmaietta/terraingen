#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <vector>

using terraingen::Crater;
using terraingen::CraterSlopeResponse;
using terraingen::CraterSlopeShape;
using terraingen::SlopeShape;
using terraingen::ValueDistribution;
using terraingen::Vec2d;

namespace {

CraterSlopeResponse Enabled(double offset = 0.1, double upslopeRim = 0.0) {
    CraterSlopeResponse response;
    response.enabled = true;
    response.floorOffset = {ValueDistribution::Kind::Constant, offset};
    response.upslopeRim = upslopeRim;
    return response;
}

}  // namespace

TEST(SlopeShape, OffsetsTheFloorDownslopeAndLowersTheRimUpslope) {
    const Crater crater{0.0, 0.0, 100.0, 1.0, 9u};
    EXPECT_TRUE(SlopeShape(crater, 0.3, 0.0, {}).Level()) << "off by default";
    EXPECT_TRUE(SlopeShape(crater, 0.0, 0.0, Enabled()).Level()) << "level ground";
    const CraterSlopeShape shape = SlopeShape(crater, 0.0, 0.5, Enabled(0.1, 0.25));
    EXPECT_FALSE(shape.Level());
    EXPECT_NEAR(shape.shift.x, 0.0, 1e-12);
    EXPECT_DOUBLE_EQ(shape.shift.y, -10.0) << "downslope is south";
    EXPECT_NEAR(shape.upslope.x, 0.0, 1e-12);
    EXPECT_DOUBLE_EQ(shape.upslope.y, 0.75);
    const double half = std::tan(0.5 * Enabled().fullSlope);
    EXPECT_DOUBLE_EQ(SlopeShape(crater, half, 0.0, Enabled()).shift.x, -10.0 * 0.5)
        << "half way up the ramp at half the full slope";
}

TEST(SlopeShape, DrawsEachCratersOffsetFromItsID) {
    const CraterSlopeResponse response = Enabled();
    CraterSlopeResponse spread = response;
    spread.floorOffset = CraterSlopeResponse{}.floorOffset;
    std::vector<double> offsets;
    for (std::uint64_t id = 1; id <= 200; ++id) {
        const Crater crater{0.0, 0.0, 10.0, 1.0, id};
        const double offset = -SlopeShape(crater, 1.0, 0.0, spread).shift.x / 10.0;
        ASSERT_GE(offset, 0.045);
        ASSERT_LE(offset, 0.194);
        ASSERT_EQ(offset, -SlopeShape(crater, 1.0, 0.0, spread).shift.x / 10.0);
        offsets.push_back(offset);
    }
    EXPECT_NE(offsets[0], offsets[1]);
}

TEST(SlopeShiftedOffset, MovesTheFloorInFullAndLeavesTheRim) {
    const CraterSlopeShape shape{{0.0, -10.0}, {0.0, 1.0}};
    const Vec2d floor = terraingen::SlopeShiftedOffset(shape, {0.0, -10.0}, 50.0);
    EXPECT_DOUBLE_EQ(floor.x, 0.0);
    EXPECT_DOUBLE_EQ(floor.y, 0.0) << "the deepest point moves to the shift";
    const Vec2d rim = terraingen::SlopeShiftedOffset(shape, {50.0, 0.0}, 50.0);
    EXPECT_DOUBLE_EQ(rim.x, 50.0);
    EXPECT_DOUBLE_EQ(rim.y, 0.0);
    const Vec2d between = terraingen::SlopeShiftedOffset(shape, {0.0, 32.5}, 50.0);
    EXPECT_DOUBLE_EQ(between.y, 32.5 + 5.0) << "half the shift half way down the fade";
}

TEST(SlopeRaisedShare, RemovesRaisedReliefUpslopeOnly) {
    const CraterSlopeShape shape{{0.0, -10.0}, {0.0, 0.8}};
    EXPECT_DOUBLE_EQ(terraingen::SlopeRaisedShare(shape, {0.0, 60.0}), 0.2);
    EXPECT_DOUBLE_EQ(terraingen::SlopeRaisedShare(shape, {60.0, 0.0}), 1.0);
    EXPECT_DOUBLE_EQ(terraingen::SlopeRaisedShare(shape, {0.0, -60.0}), 1.0);
    EXPECT_DOUBLE_EQ(terraingen::SlopeRaisedShare(shape, {0.0, 0.0}), 1.0);
    EXPECT_NEAR(terraingen::SlopeRaisedShare(shape, {60.0, 60.0}), 1.0 - 0.8 * 0.5, 1e-12);
    EXPECT_DOUBLE_EQ(terraingen::SlopeRaisedShare({}, {0.0, 60.0}), 1.0);
}

TEST(ValidateCraterSlopeResponse, RejectsInvalidSettings) {
    EXPECT_NO_THROW(terraingen::ValidateCraterSlopeResponse({}));
    EXPECT_THROW(terraingen::ValidateCraterSlopeResponse(Enabled(0.25)), std::invalid_argument);
    EXPECT_THROW(terraingen::ValidateCraterSlopeResponse(Enabled(-0.01)), std::invalid_argument);
    EXPECT_THROW(terraingen::ValidateCraterSlopeResponse(Enabled(0.1, 1.5)), std::invalid_argument);
    CraterSlopeResponse flat = Enabled();
    flat.fullSlope = 0.0;
    EXPECT_THROW(terraingen::ValidateCraterSlopeResponse(flat), std::invalid_argument);
    terraingen::CraterApplyOptions options;
    options.slope = Enabled();
    EXPECT_THROW(terraingen::ValidateCraterApplyOptions(options), std::invalid_argument)
        << "needs the plane datum";
    options.datum = terraingen::CraterDatum::Plane;
    EXPECT_NO_THROW(terraingen::ValidateCraterApplyOptions(options));
}

TEST(ApplyCraters, OffsetsFloorsDownslopeAndLowersRimsUpslopeOnSlopingGround) {
    const auto profile = std::make_shared<terraingen::FassettThomsonProfile>();
    const terraingen::CraterShapeModels models{
        profile,
        std::make_shared<terraingen::NoDegradation>(terraingen::LunarFreshCraterMorphometry)};
    const double rise = std::tan(15.0 * std::numbers::pi / 180.0);
    terraingen::Raster<float> base(201, 201, 0.0f);
    base.SetTransform(terraingen::GeoTransform::NorthUp(-100.5, 100.5, 1.0));
    for (std::size_t row = 0; row < base.Height(); ++row) {
        for (std::size_t col = 0; col < base.Width(); ++col) {
            base(col, row) = static_cast<float>(rise * base.CellCenter(col, row).y);
        }
    }
    terraingen::CraterList craters;
    craters.Append({0.0, 0.0, 140.0, 0.0, 1u});
    terraingen::CraterApplyOptions options;
    options.datum = terraingen::CraterDatum::Plane;
    const auto relief = [&](const terraingen::CraterApplyOptions& o) {
        terraingen::Raster<float> heights = base;
        terraingen::ApplyCraters(heights, craters, models, o, {});
        std::vector<double> column(heights.Height());
        for (std::size_t row = 0; row < heights.Height(); ++row) {
            column[row] = heights(100, row) - base(100, row);
        }
        return column;
    };
    const auto floorRow = [](const std::vector<double>& column) {
        const double deepest = *std::min_element(column.begin(), column.end());
        double sum = 0.0;
        int count = 0;
        for (std::size_t row = 0; row < column.size(); ++row) {
            if (column[row] < 0.95 * deepest) {
                sum += static_cast<double>(row);
                ++count;
            }
        }
        return sum / count;
    };
    const std::vector<double> level = relief(options);
    options.slope = Enabled(0.1);
    const std::vector<double> sloped = relief(options);
    EXPECT_NEAR(floorRow(level), 100.0, 0.5);
    EXPECT_GT(floorRow(sloped), 100.0 + 8.0) << "the floor moves south, downslope";
    const double upslopeRim = *std::max_element(sloped.begin(), sloped.begin() + 100);
    const double downslopeRim = *std::max_element(sloped.begin() + 100, sloped.end());
    EXPECT_LT(upslopeRim, 0.05 * downslopeRim);
    EXPECT_NEAR(downslopeRim, *std::max_element(level.begin() + 100, level.end()),
                0.05 * downslopeRim);
}

TEST(SlopeWearRate, FollowsRoeringsNonlinearTransportUpToItsCap) {
    CraterSlopeResponse response;
    EXPECT_EQ(terraingen::SlopeWearRate(response, 0.5, 0.0), 1.0) << "no critical gradient";
    response.criticalGradient = 1.0;
    EXPECT_DOUBLE_EQ(terraingen::SlopeWearRate(response, 0.0, 0.0), 1.0);
    EXPECT_DOUBLE_EQ(terraingen::SlopeWearRate(response, 0.3, 0.4), 1.0 / 0.75);
    EXPECT_DOUBLE_EQ(terraingen::SlopeWearRate(response, 0.99, 0.0), 10.0) << "capped";
    EXPECT_DOUBLE_EQ(terraingen::SlopeWearRate(response, 2.0, 0.0), 10.0);
    response.criticalGradient = 0.0;
    EXPECT_THROW(terraingen::ValidateCraterSlopeResponse(response), std::invalid_argument);
    response.criticalGradient = 1.0;
    response.maxWearRate = 0.5;
    EXPECT_THROW(terraingen::ValidateCraterSlopeResponse(response), std::invalid_argument);
    terraingen::CraterApplyOptions options;
    options.slope.criticalGradient = 1.0;
    EXPECT_THROW(terraingen::ValidateCraterApplyOptions(options), std::invalid_argument)
        << "needs the plane datum";
}

TEST(DegradationModel, WearsFasterAsIfOlderOrWithMoreDiffusion) {
    const terraingen::Crater crater{0.0, 0.0, 200.0, 0.5, 3u};
    const terraingen::FreshnessDegradation freshness(std::make_shared<terraingen::UniformAges>(2.0),
                                                     terraingen::LunarFreshCraterMorphometry);
    EXPECT_EQ(freshness.WornState(crater, 3.0), freshness.State({0.0, 0.0, 200.0, 1.5, 3u}))
        << "by default, as a crater 3 times older";
    const auto diffused = std::make_shared<terraingen::DiffusedProfile>(
        std::make_shared<terraingen::FassettThomsonProfile>());
    const terraingen::DiffusionDegradation diffusion(terraingen::LunarFreshCraterMorphometry,
                                                     terraingen::Fassett2022HartmannDiffusivity(),
                                                     diffused);
    const terraingen::CraterState level = diffusion.State(crater);
    const terraingen::CraterState steep = diffusion.WornState(crater, 3.0);
    EXPECT_NEAR(steep.diffusion, 3.0 * level.diffusion, 1e-12 * steep.diffusion);
    EXPECT_LT(steep.current.depthRatio, level.current.depthRatio);
}

TEST(ApplyCraters, WearsCratersOnSteepGroundFaster) {
    const auto diffused = std::make_shared<terraingen::DiffusedProfile>(
        std::make_shared<terraingen::FassettThomsonProfile>());
    const terraingen::CraterShapeModels models{
        diffused, std::make_shared<terraingen::DiffusionDegradation>(
                      terraingen::LunarFreshCraterMorphometry,
                      terraingen::Fassett2022HartmannDiffusivity(), diffused)};
    terraingen::Raster<float> base(161, 161, 0.0f);
    base.SetTransform(terraingen::GeoTransform::NorthUp(-80.5, 80.5, 1.0));
    for (std::size_t row = 0; row < base.Height(); ++row) {
        for (std::size_t col = 0; col < base.Width(); ++col) {
            base(col, row) = static_cast<float>(0.4 * base.CellCenter(col, row).y);
        }
    }
    terraingen::CraterList craters;
    craters.Append({0.0, 0.0, 100.0, 0.8, 1u});
    terraingen::CraterApplyOptions options;
    options.datum = terraingen::CraterDatum::Plane;
    const auto depth = [&](const terraingen::CraterApplyOptions& o) {
        terraingen::Raster<float> heights = base;
        terraingen::ApplyCraters(heights, craters, models, o, {});
        double deepest = 0.0;
        for (std::size_t i = 0; i < heights.Size(); ++i) {
            deepest = std::fmin(deepest, static_cast<double>(heights.Values()[i]) -
                                             static_cast<double>(base.Values()[i]));
        }
        return -deepest;
    };
    const double level = depth(options);
    options.slope.criticalGradient = 0.7;
    const double steep = depth(options);
    EXPECT_LT(steep, 0.9 * level) << "a gradient of 0.4 wears it about 1.5 times as fast";
}

TEST(ApplyCraters, FitsDatumsToTheSlopeShapedReliefSoNoCraterStandsAsAMesa) {
    const auto profile = std::make_shared<terraingen::FassettThomsonProfile>();
    const terraingen::CraterShapeModels models{
        profile,
        std::make_shared<terraingen::NoDegradation>(terraingen::LunarFreshCraterMorphometry)};
    const double rise = std::tan(15.0 * std::numbers::pi / 180.0);
    terraingen::Raster<float> base(241, 241, 0.0f);
    base.SetTransform(terraingen::GeoTransform::NorthUp(-120.5, 120.5, 1.0));
    for (std::size_t row = 0; row < base.Height(); ++row) {
        for (std::size_t col = 0; col < base.Width(); ++col) {
            base(col, row) = static_cast<float>(rise * base.CellCenter(col, row).y);
        }
    }
    terraingen::CraterList craters;
    craters.Append({0.0, 0.0, 200.0, 2.0, 1u});
    craters.Append({0.0, -45.0, 16.0, 0.1, 2u});
    terraingen::CraterApplyOptions options;
    options.datum = terraingen::CraterDatum::Plane;
    options.slope = Enabled(0.15);
    terraingen::Raster<float> both = base;
    terraingen::ApplyCraters(both, craters, models, options, {});
    terraingen::CraterList large;
    large.Append(craters.At(0));
    terraingen::Raster<float> alone = base;
    terraingen::ApplyCraters(alone, large, models, options, {});
    double inside = -1e9;
    double around = -1e9;
    for (std::size_t row = 0; row < both.Height(); ++row) {
        for (std::size_t col = 0; col < both.Width(); ++col) {
            const Vec2d p = both.CellCenter(col, row);
            const double radii = std::hypot(p.x, p.y + 45.0) / 8.0;
            const double change = both(col, row) - alone(col, row);
            if (radii < 0.7) {
                inside = std::fmax(inside, change);
            } else if (radii > 1.6 && radii < 2.0) {
                around = std::fmax(around, std::fabs(change));
            }
        }
    }
    EXPECT_LT(inside, 0.0) << "the small crater is a hollow in the shifted floor, not a mesa";
    EXPECT_LT(around, 0.4) << "and leaves its surroundings much as they are";
}
