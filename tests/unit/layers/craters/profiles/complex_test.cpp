#include "terraingen/layers/craters/profiles/complex.h"

#include "terraingen/terraingen.h"
#include "terraingen/util/bits.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <stdexcept>

using terraingen::ComplexCraterProfile;
using terraingen::CraterState;

namespace {

ComplexCraterProfile Lunar() {
    return {std::make_shared<terraingen::FassettThomsonProfile>(),
            terraingen::LunarComplexCraterShape()};
}

CraterState Fresh(double diameter) {
    const auto morphometry = terraingen::LunarFreshCraterMorphometry(diameter);
    return {morphometry, morphometry};
}

}  // namespace

TEST(ComplexCraterProfile, KeepsTheSimpleProfileBelowTheTransition) {
    const auto profile = Lunar();
    const terraingen::FassettThomsonProfile simple;
    const double d = 10000.0;
    for (const double r : {0.0, 1000.0, 4000.0, 5200.0, 9000.0}) {
        EXPECT_EQ(profile.Height(r, d, Fresh(d)), simple.Height(r, d, Fresh(d)));
    }
    EXPECT_FALSE(profile.IsComplex(d));
    EXPECT_TRUE(profile.IsComplex(15000.0));
    EXPECT_EQ(profile.ReachRadii(), 3.0);
}

TEST(ComplexCraterProfile, HasAFlatFloorACentralPeakAndAContinuousRim) {
    const auto profile = Lunar();
    const double d = 30000.0;
    const auto state = Fresh(d);
    const double depth = 1044.0 * std::pow(30.0, 0.301);
    const double rim = 236.0 * std::pow(30.0, 0.399);
    EXPECT_NEAR(profile.Height(15000.0, d, state), rim, 1e-7 * d);
    EXPECT_NEAR(profile.Height(15000.0 + 1e-6, d, state), rim, 1e-3) << "continuous at the rim";
    const double floorRadius = 0.5 * 187.0 * std::pow(30.0, 1.249);
    const double peak = 0.589 * std::pow(30.0, 1.969);
    const double volume = 0.987e2 * std::pow(30.0, 5.078);
    const double base = std::sqrt(3.0 * volume / (terraingen::kPi * peak));
    ASSERT_LT(base, floorRadius);
    EXPECT_NEAR(profile.Height(0.5 * (base + floorRadius), d, state), rim - depth, 1e-7 * d)
        << "flat between the peak and the wall";
    EXPECT_NEAR(profile.Height(0.0, d, state), rim - depth + peak, 1e-7 * d);
    double sum = 0.0;
    const double step = base / 2000.0;
    for (double r = 0.5 * step; r < base; r += step) {
        sum += 2.0 * terraingen::kPi * r * step * (profile.Height(r, d, state) - (rim - depth));
    }
    EXPECT_NEAR(sum, volume, 1e-3 * volume) << "the peak holds Hale and Grieve's volume";
    EXPECT_GT(profile.Height(0.95 * 15000.0, d, state), profile.Height(0.8 * 15000.0, d, state));
    EXPECT_GT(profile.Height(20000.0, d, state), 0.0);
    EXPECT_EQ(profile.Height(45000.0, d, state), 0.0) << "nothing from three radii";
}

TEST(ComplexCraterProfile, LowersThePeakAsTheCraterWearsAndHoldsItAboveTheFit) {
    const auto profile = Lunar();
    const double d = 30000.0;
    CraterState worn = Fresh(d);
    worn.current.depthRatio *= 0.5;
    worn.current.rimHeightRatio *= 0.5;
    const double freshPeak = profile.Height(0.0, d, Fresh(d)) - profile.Height(9000.0, d, Fresh(d));
    const double wornPeak = profile.Height(0.0, d, worn) - profile.Height(9000.0, d, worn);
    EXPECT_NEAR(wornPeak, 0.5 * freshPeak, 1e-7 * d);
    const double big = 100000.0;
    const auto state = Fresh(big);
    const double floor = profile.Height(0.3 * 50000.0, big, state);
    EXPECT_NEAR(profile.Height(0.0, big, state) - floor, 0.589 * std::pow(51.0, 1.969), 1e-7 * big)
        << "the peak stops growing past the fitted sizes";
}

TEST(ComplexCraterProfile, RejectsInvalidShapes) {
    EXPECT_THROW(ComplexCraterProfile(nullptr, terraingen::LunarComplexCraterShape()),
                 std::invalid_argument);
    auto shape = terraingen::LunarComplexCraterShape();
    shape.transitionDiameter = 0.0;
    EXPECT_THROW(terraingen::ValidateComplexCraterShape(shape), std::invalid_argument);
    shape = terraingen::LunarComplexCraterShape();
    shape.peakVolume.coefficient = -1.0;
    EXPECT_THROW(terraingen::ValidateComplexCraterShape(shape), std::invalid_argument);
    shape = terraingen::LunarComplexCraterShape();
    shape.peakFitMaxDiameter = -1.0;
    EXPECT_THROW(terraingen::ValidateComplexCraterShape(shape), std::invalid_argument);
    shape = terraingen::LunarComplexCraterShape();
    shape.reachRadii = 1.0;
    EXPECT_THROW(terraingen::ValidateComplexCraterShape(shape), std::invalid_argument);
    EXPECT_EQ((terraingen::KilometerPowerLaw{2.0, 1.0}.At(3000.0)), 6000.0);
    EXPECT_EQ((terraingen::KilometerPowerLaw{2.0, 1.0}.At(3000.0, true)), 6e9);
}
