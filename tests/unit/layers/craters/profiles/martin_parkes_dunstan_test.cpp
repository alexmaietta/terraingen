#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <stdexcept>

using terraingen::CraterMorphometry;
using terraingen::CraterProfile;
using terraingen::CraterState;
using terraingen::FassettThomsonProfile;
using terraingen::FassettThomsonRim;
using terraingen::kLunarFreshRimHeightRatio;
using terraingen::MartinParkesDunstanProfile;

namespace {

CraterState Fresh(double depthRatio, double rimRatio = kLunarFreshRimHeightRatio) {
    const CraterMorphometry shape{depthRatio, rimRatio};
    return {shape, shape};
}

}  // namespace

TEST(MartinParkesDunstanProfile, FreshCraterHasABowlAndASharpRim) {
    const MartinParkesDunstanProfile profile;
    const double d = 100.0;
    const CraterState fresh = Fresh(0.2, 0.036);
    EXPECT_NEAR(profile.Height(0.0, d, fresh), (0.036 - 0.2) * d, 1e-7 * d);
    EXPECT_NEAR(profile.Height(50.0, d, fresh), 0.036 * d, 1e-7 * d);
    EXPECT_NEAR(profile.Height(25.0, d, fresh), (0.2 * 0.25 - 0.2 + 0.036) * d, 1e-7 * d);
    EXPECT_NEAR(profile.Height(50.0 + 1e-9, d, fresh), 0.036 * d, 1e-6);
    EXPECT_NEAR(profile.Height(100.0 - 1e-9, d, fresh), 0.0, 1e-6);
    EXPECT_EQ(profile.Height(100.0, d, fresh), 0.0);
    EXPECT_EQ(profile.ReachRadii(), 2.0);
}

TEST(MartinParkesDunstanProfile, DegradedCraterHasARoundedContinuousRim) {
    const MartinParkesDunstanProfile profile;
    const double d = 100.0;
    const CraterState worn{{0.2, 0.036}, {0.12, 0.6 * 0.036}};
    const double k = 0.036 * d - 0.6 * 0.036 * d + 0.12 * d;
    const double alpha = (0.6 * 0.036 * d - 0.036 * d) / k;
    const double beta = 1.5 * (0.036 * d - 0.6 * 0.036 * d) / k;
    ASSERT_LT(alpha, 0.0);
    ASSERT_GT(beta, 0.0);
    EXPECT_NEAR(profile.Height(50.0, d, worn), 0.6 * 0.036 * d, 1e-7 * d);
    EXPECT_NEAR(profile.Height(0.0, d, worn), (0.6 * 0.036 - 0.12) * d, 1e-7 * d);
    for (const double x : {alpha, 0.0, beta}) {
        const double r = (x + 1.0) * 50.0;
        EXPECT_NEAR(profile.Height(r - 1e-7, d, worn), profile.Height(r + 1e-7, d, worn), 1e-5)
            << x;
    }
    const double slope =
        (profile.Height(50.0 + 1e-4, d, worn) - profile.Height(50.0 - 1e-4, d, worn));
    EXPECT_NEAR(slope, 0.0, 1e-6);
    EXPECT_GT(profile.Height(50.0, d, worn), profile.Height(45.0, d, worn));
    EXPECT_GT(profile.Height(50.0, d, worn), profile.Height(55.0, d, worn));
}

TEST(MartinParkesDunstanProfile, RejectsInconsistentStates) {
    const MartinParkesDunstanProfile profile;
    EXPECT_THROW(profile.Height(0.0, 10.0, Fresh(0.0, 0.036)), std::invalid_argument);
    EXPECT_THROW(profile.Height(0.0, 10.0, CraterState{{0.2, 0.036}, {0.2, 0.04}}),
                 std::invalid_argument);
    EXPECT_THROW(profile.Height(0.0, 10.0, CraterState{{0.2, 0.036}, {0.2, -0.01}}),
                 std::invalid_argument);
    const std::unique_ptr<CraterProfile> base = std::make_unique<MartinParkesDunstanProfile>();
    EXPECT_EQ(base->ReachRadii(), 2.0);
}
