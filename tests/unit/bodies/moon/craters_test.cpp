#include "terraingen/bodies/moon/craters.h"

#include <gtest/gtest.h>

#include <cmath>
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

TEST(LunarFreshCraterMorphometry, FollowsPikesComplexLawsFromFifteenKilometres) {
    const auto complex = terraingen::LunarFreshCraterMorphometry(30000.0);
    EXPECT_NEAR(complex.depthRatio * 30.0, 1.044 * std::pow(30.0, 0.301), 1e-12);
    EXPECT_NEAR(complex.rimHeightRatio * 30.0, 0.236 * std::pow(30.0, 0.399), 1e-12);
    EXPECT_EQ(terraingen::LunarFreshCraterMorphometry(14999.0).depthRatio, 0.21);
    const auto shape = terraingen::LunarComplexCraterShape();
    EXPECT_EQ(shape.transitionDiameter, terraingen::kLunarComplexCraterDiameter);
    EXPECT_EQ(shape.floorDiameter, (terraingen::KilometrePowerLaw{0.187, 1.249}));
    EXPECT_NO_THROW(terraingen::ValidateComplexCraterShape(shape));
}
