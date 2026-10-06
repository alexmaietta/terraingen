#include "terraingen/bodies/moon/craters.h"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

using terraingen::LunarFreshCraterDepthRatio;

TEST(LunarFreshCraterDepthRatio, FollowsFassett2022Table2) {
    EXPECT_EQ(LunarFreshCraterDepthRatio(1.0), 0.10);
    EXPECT_EQ(LunarFreshCraterDepthRatio(10.0), 0.10);
    EXPECT_EQ(LunarFreshCraterDepthRatio(10.5), 0.11);
    EXPECT_EQ(LunarFreshCraterDepthRatio(40.0), 0.13);
    EXPECT_EQ(LunarFreshCraterDepthRatio(150.0), 0.15);
    EXPECT_EQ(LunarFreshCraterDepthRatio(399.0), 0.17);
    EXPECT_EQ(LunarFreshCraterDepthRatio(400.0), 0.21);
    EXPECT_EQ(LunarFreshCraterDepthRatio(5000.0), 0.21);
    EXPECT_THROW(LunarFreshCraterDepthRatio(0.0), std::domain_error);
    EXPECT_THROW(LunarFreshCraterDepthRatio(std::numeric_limits<double>::quiet_NaN()),
                 std::domain_error);
}

TEST(LunarFreshCraterMorphometry, PairsTheDepthRatioWithTheRimRatio) {
    const auto morphometry = terraingen::LunarFreshCraterMorphometry(150.0);
    EXPECT_EQ(morphometry.depthRatio, 0.15);
    EXPECT_EQ(morphometry.rimHeightRatio, terraingen::kLunarFreshRimHeightRatio);
    EXPECT_THROW(terraingen::LunarFreshCraterMorphometry(-1.0), std::domain_error);
}
