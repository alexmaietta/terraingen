#include "terraingen/features/craters/degradation.h"

#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <stdexcept>

using terraingen::Crater;
using terraingen::CraterState;
using terraingen::DegradationModel;
using terraingen::FreshCraterDepthRatio;
using terraingen::FreshnessDegradation;
using terraingen::kFreshRimHeightRatio;
using terraingen::NoDegradation;
using terraingen::UniformFreshness;

TEST(FreshCraterDepthRatio, FollowsFassett2022Table2) {
    EXPECT_EQ(FreshCraterDepthRatio(1.0), 0.10);
    EXPECT_EQ(FreshCraterDepthRatio(10.0), 0.10);
    EXPECT_EQ(FreshCraterDepthRatio(10.5), 0.11);
    EXPECT_EQ(FreshCraterDepthRatio(40.0), 0.13);
    EXPECT_EQ(FreshCraterDepthRatio(150.0), 0.15);
    EXPECT_EQ(FreshCraterDepthRatio(399.0), 0.17);
    EXPECT_EQ(FreshCraterDepthRatio(400.0), 0.21);
    EXPECT_EQ(FreshCraterDepthRatio(5000.0), 0.21);
    EXPECT_THROW(FreshCraterDepthRatio(0.0), std::domain_error);
    EXPECT_THROW(FreshCraterDepthRatio(std::numeric_limits<double>::quiet_NaN()),
                 std::domain_error);
}

TEST(NoDegradation, KeepsEveryCraterFresh) {
    const NoDegradation fresh;
    const CraterState state = fresh.State(Crater{0.0, 0.0, 150.0, 3.0, 1u});
    EXPECT_EQ(state.fresh.depthRatio, 0.15);
    EXPECT_EQ(state.fresh.rimHeightRatio, kFreshRimHeightRatio);
    EXPECT_EQ(state.current.depthRatio, state.fresh.depthRatio);
    EXPECT_EQ(state.current.rimHeightRatio, state.fresh.rimHeightRatio);
    const NoDegradation fixed(0.2);
    EXPECT_EQ(fixed.State(Crater{0.0, 0.0, 5.0, 0.0, 1u}).current.depthRatio, 0.2);
    EXPECT_THROW(NoDegradation(0.0), std::invalid_argument);
    EXPECT_THROW(NoDegradation(std::numeric_limits<double>::infinity()), std::invalid_argument);
    EXPECT_THROW(fresh.State(Crater{0.0, 0.0, -1.0, 0.0, 1u}), std::domain_error);
    const std::unique_ptr<DegradationModel> base = std::make_unique<NoDegradation>();
    EXPECT_EQ(base->State(Crater{0.0, 0.0, 500.0, 0.0, 1u}).current.depthRatio, 0.21);
}

TEST(FreshnessDegradation, ScalesDepthAndRimByOneErosionFactor) {
    const FreshnessDegradation degradation(std::make_shared<UniformFreshness>(2.0));
    const auto at = [&](double ageGyr) {
        return degradation.State(Crater{0.0, 0.0, 150.0, ageGyr, 1u});
    };
    const CraterState fresh = at(0.0);
    EXPECT_DOUBLE_EQ(fresh.current.depthRatio, 0.15);
    EXPECT_DOUBLE_EQ(fresh.current.rimHeightRatio, kFreshRimHeightRatio);
    const CraterState worn = at(2.0);
    EXPECT_DOUBLE_EQ(worn.current.depthRatio, 0.04);
    EXPECT_DOUBLE_EQ(worn.current.rimHeightRatio, kFreshRimHeightRatio * 0.04 / 0.15);
    EXPECT_EQ(worn.fresh.depthRatio, 0.15);
    const CraterState half = at(1.0);
    EXPECT_DOUBLE_EQ(half.current.depthRatio, 0.5 * (0.15 + 0.04));
    EXPECT_DOUBLE_EQ(half.current.depthRatio / half.fresh.depthRatio,
                     half.current.rimHeightRatio / half.fresh.rimHeightRatio);
    EXPECT_THROW(FreshnessDegradation(nullptr), std::invalid_argument);
    EXPECT_THROW(FreshnessDegradation(std::make_shared<UniformFreshness>(1.0), 0.0),
                 std::invalid_argument);
    EXPECT_THROW(FreshnessDegradation(std::make_shared<UniformFreshness>(1.0), 0.1),
                 std::invalid_argument);
}
