#include "terraingen/layers/craters/shaping/morphometry.h"

#include "terraingen/layers/craters/profiles/fassett_thomson.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

using terraingen::LunarFreshCraterDepthRatio;
using terraingen::MarsFreshCraterMorphometry;

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

TEST(LunarFreshCraterMorphometry, FollowsPikesComplexLawsFromFifteenKilometers) {
    const auto complex = terraingen::LunarFreshCraterMorphometry(30000.0);
    EXPECT_NEAR(complex.depthRatio * 30.0, 1.044 * std::pow(30.0, 0.301), 1e-6);
    EXPECT_NEAR(complex.rimHeightRatio * 30.0, 0.236 * std::pow(30.0, 0.399), 1e-6);
    EXPECT_EQ(terraingen::LunarFreshCraterMorphometry(14999.0).depthRatio, 0.21);
    const auto shape = terraingen::LunarComplexCraterShape();
    EXPECT_EQ(shape.transitionDiameter, terraingen::kLunarComplexCraterDiameter);
    EXPECT_EQ(shape.floorDiameter, (terraingen::KilometerPowerLaw{0.187, 1.249}));
    EXPECT_NO_THROW(terraingen::ValidateComplexCraterShape(shape));
}

TEST(MarsFreshCraterMorphometry, FollowsSmallFreshCratersThenGarvinsFits) {
    EXPECT_EQ(MarsFreshCraterMorphometry(30.0).depthRatio, 0.23) << "Daubar et al. 2014";
    EXPECT_EQ(MarsFreshCraterMorphometry(30.0).rimHeightRatio, 0.03) << "Sweeney et al. 2018";
    EXPECT_NEAR(MarsFreshCraterMorphometry(3000.0).depthRatio * 3.0, 0.21 * std::pow(3.0, 0.81),
                1e-6);
    EXPECT_NEAR(MarsFreshCraterMorphometry(3000.0).rimHeightRatio * 3.0, 0.04 * std::pow(3.0, 0.31),
                1e-6);
    EXPECT_NEAR(MarsFreshCraterMorphometry(20000.0).depthRatio * 20.0, 0.36 * std::pow(20.0, 0.49),
                1e-6);
    EXPECT_NEAR(MarsFreshCraterMorphometry(20000.0).rimHeightRatio * 20.0,
                0.02 * std::pow(20.0, 0.84), 1e-6);
    EXPECT_NEAR(MarsFreshCraterMorphometry(200000.0).rimHeightRatio * 200.0,
                0.12 * std::pow(200.0, 0.35), 1e-6);
    double previous = 1.0;
    for (double d = 10.0; d < 6900.0; d *= 1.1) {
        const double ratio = MarsFreshCraterMorphometry(d).depthRatio;
        EXPECT_LE(ratio, previous) << d;
        previous = ratio;
    }
    EXPECT_THROW(MarsFreshCraterMorphometry(0.0), std::domain_error);
}

TEST(MarsComplexCraterShape, GivesGarvinsCentralPeaks) {
    const terraingen::ComplexCraterProfile profile(
        std::make_shared<terraingen::FassettThomsonProfile>(),
        terraingen::MarsComplexCraterShape());
    const double d = 20000.0;
    const auto m = MarsFreshCraterMorphometry(d);
    const terraingen::CraterState state{m, m};
    const double floor = (m.rimHeightRatio - m.depthRatio) * d;
    const double peak = 40.0 * std::pow(20.0, 0.51);
    const double base = 125.0 * std::pow(20.0, 1.05);
    EXPECT_NEAR(profile.Height(0.0, d, state), floor + peak, 1e-7 * d);
    EXPECT_NEAR(profile.Height(base, d, state), floor, 1e-7 * d) << "the peak's base";
    EXPECT_TRUE(profile.IsComplex(7000.0));
    EXPECT_FALSE(profile.IsComplex(6999.0));
    auto shape = terraingen::MarsComplexCraterShape();
    shape.peakDiameter->coefficient = 0.0;
    EXPECT_THROW(terraingen::ValidateComplexCraterShape(shape), std::invalid_argument);
}
