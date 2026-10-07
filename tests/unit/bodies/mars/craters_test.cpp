#include "terraingen/bodies/mars/craters.h"

#include "terraingen/layers/craters/profiles/fassett_thomson.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <stdexcept>

using terraingen::MarsFreshCraterMorphometry;

TEST(MarsFreshCraterMorphometry, FollowsSmallFreshCratersThenGarvinsFits) {
    EXPECT_EQ(MarsFreshCraterMorphometry(30.0).depthRatio, 0.23) << "Daubar et al. 2014";
    EXPECT_EQ(MarsFreshCraterMorphometry(30.0).rimHeightRatio, 0.03) << "Sweeney et al. 2018";
    EXPECT_NEAR(MarsFreshCraterMorphometry(3000.0).depthRatio * 3.0, 0.21 * std::pow(3.0, 0.81),
                1e-12);
    EXPECT_NEAR(MarsFreshCraterMorphometry(3000.0).rimHeightRatio * 3.0, 0.04 * std::pow(3.0, 0.31),
                1e-12);
    EXPECT_NEAR(MarsFreshCraterMorphometry(20000.0).depthRatio * 20.0, 0.36 * std::pow(20.0, 0.49),
                1e-12);
    EXPECT_NEAR(MarsFreshCraterMorphometry(20000.0).rimHeightRatio * 20.0,
                0.02 * std::pow(20.0, 0.84), 1e-12);
    EXPECT_NEAR(MarsFreshCraterMorphometry(200000.0).rimHeightRatio * 200.0,
                0.12 * std::pow(200.0, 0.35), 1e-12);
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
    EXPECT_NEAR(profile.Height(0.0, d, state), floor + peak, 1e-6);
    EXPECT_NEAR(profile.Height(base, d, state), floor, 1e-6) << "the peak's base";
    EXPECT_TRUE(profile.IsComplex(7000.0));
    EXPECT_FALSE(profile.IsComplex(6999.0));
    auto shape = terraingen::MarsComplexCraterShape();
    shape.peakDiameter->coefficient = 0.0;
    EXPECT_THROW(terraingen::ValidateComplexCraterShape(shape), std::invalid_argument);
}
