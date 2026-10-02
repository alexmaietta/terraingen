#include "terraingen/features/craters/profiles.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <stdexcept>

using terraingen::CraterMorphometry;
using terraingen::CraterProfile;
using terraingen::CraterState;
using terraingen::FassettThomsonProfile;
using terraingen::FassettThomsonRim;
using terraingen::kFreshRimHeightRatio;
using terraingen::MartinParkesDunstanProfile;

namespace {

CraterState Fresh(double depthRatio, double rimRatio = kFreshRimHeightRatio) {
    const CraterMorphometry shape{depthRatio, rimRatio};
    return {shape, shape};
}

double Interior(double x) {
    return -0.228809953 + 0.227533882 * x + 0.083116795 * x * x - 0.039499407 * x * x * x;
}

double Exterior(double x) {
    return 0.188253307 - 0.187050452 * x + 0.01844746 * x * x + 0.01505647 * x * x * x;
}

}  // namespace

TEST(FassettThomsonProfile, EvaluatesTheFitsWithAFlatFloor) {
    const FassettThomsonProfile profile;
    const double d = 100.0;
    const CraterState deep = Fresh(kFreshRimHeightRatio + 0.181);
    EXPECT_NEAR(profile.Height(0.0, d, deep), -0.181 * d, 1e-12);
    EXPECT_NEAR(profile.Height(0.6 * 50.0, d, deep), Interior(0.6) * d, 1e-12);
    EXPECT_NEAR(profile.Height(1.2 * 50.0, d, deep), Exterior(1.2) * d, 1e-12);
    EXPECT_EQ(profile.Height(1.51 * 50.0, d, deep), 0.0);
    EXPECT_EQ(profile.ReachRadii(), 1.5);
    const CraterState shallow = Fresh(0.13);
    EXPECT_NEAR(profile.Height(0.0, d, shallow), (kFreshRimHeightRatio - 0.13) * d, 1e-12);
    EXPECT_NEAR(profile.Height(0.9 * 50.0, d, shallow), Interior(0.9) * d, 1e-12);
}

TEST(FassettThomsonProfile, RimVariantsJoinAsPublished) {
    const double d = 10.0;
    const CraterState state = Fresh(0.2);
    const auto step = [&](const FassettThomsonProfile& profile, double x) {
        return profile.Height((x + 1e-9) * 0.5 * d, d, state) -
               profile.Height((x - 1e-9) * 0.5 * d, d, state);
    };
    const FassettThomsonProfile original(FassettThomsonRim::Original);
    EXPECT_NEAR(step(original, 0.98), 0.0, 2e-5 * d);
    const FassettThomsonProfile atOne(FassettThomsonRim::AtOneRadius);
    EXPECT_NEAR(atOne.Height(0.99 * 0.5 * d, d, state), Interior(0.99) * d, 1e-12);
    EXPECT_NEAR(step(atOne, 1.0), (Exterior(1.0) - Interior(1.0)) * d, 1e-6);
    EXPECT_NEAR(step(atOne, 1.0) / d, -0.0076, 1e-4);
    const FassettThomsonProfile plateau(FassettThomsonRim::Plateau);
    EXPECT_NEAR(plateau.Height(0.5 * d, d, state), kFreshRimHeightRatio * d, 1e-12);
    EXPECT_NEAR(plateau.Height(0.7 * 0.5 * d, d, state), Interior(0.7) * d, 1e-12);
    EXPECT_NEAR(plateau.Height(0.0, d, state), (kFreshRimHeightRatio - 0.2) * d, 1e-12);
    EXPECT_NEAR(step(plateau, 1.02) / d, -0.0042, 1e-4);
    double previous = original.Height(0.0, d, state);
    for (int i = 1; i <= 97; ++i) {
        const double h = original.Height(i * 0.01 * 0.5 * d, d, state);
        EXPECT_GE(h, previous - 1e-12) << i;
        previous = h;
    }
}

TEST(MartinParkesDunstanProfile, FreshCraterHasABowlAndASharpRim) {
    const MartinParkesDunstanProfile profile;
    const double d = 100.0;
    const CraterState fresh = Fresh(0.2, 0.036);
    EXPECT_NEAR(profile.Height(0.0, d, fresh), (0.036 - 0.2) * d, 1e-9);
    EXPECT_NEAR(profile.Height(50.0, d, fresh), 0.036 * d, 1e-9);
    EXPECT_NEAR(profile.Height(25.0, d, fresh), (0.2 * 0.25 - 0.2 + 0.036) * d, 1e-9);
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
    EXPECT_NEAR(profile.Height(50.0, d, worn), 0.6 * 0.036 * d, 1e-9);
    EXPECT_NEAR(profile.Height(0.0, d, worn), (0.6 * 0.036 - 0.12) * d, 1e-9);
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
