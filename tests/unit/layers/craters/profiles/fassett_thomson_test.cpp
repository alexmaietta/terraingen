// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
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

double Interior(double x) {
    return -0.228809953 + 0.227533882 * x + 0.083116795 * x * x - 0.039499407 * x * x * x;
}

double Exterior(double x) {
    return 0.188253307 - 0.187050452 * x + 0.01844746 * x * x + 0.01505647 * x * x * x;
}

/// `Exterior` tapered to a zero slope at 1.5 radii from where it starts at `start` radii.
double TaperedExterior(double x, double start) {
    const double u = (x - start) / (1.5 - start);
    return Exterior(x) * (1.0 - u * u);
}

}  // namespace

TEST(FassettThomsonProfile, EvaluatesTheFitsWithAFlatFloor) {
    const FassettThomsonProfile profile;
    const double d = 100.0;
    const CraterState deep = Fresh(kLunarFreshRimHeightRatio + 0.181);
    EXPECT_NEAR(profile.Height(0.0, d, deep), -0.181 * d, 1e-7 * d);
    EXPECT_NEAR(profile.Height(0.6 * 50.0, d, deep), Interior(0.6) * d, 1e-7 * d);
    EXPECT_NEAR(profile.Height(1.2 * 50.0, d, deep), TaperedExterior(1.2, 0.98) * d, 1e-7 * d);
    EXPECT_EQ(profile.Height(1.51 * 50.0, d, deep), 0.0);
    EXPECT_EQ(profile.ReachRadii(), 1.5);
    const CraterState shallow = Fresh(0.13);
    EXPECT_NEAR(profile.Height(0.0, d, shallow), (kLunarFreshRimHeightRatio - 0.13) * d, 1e-7 * d);
    EXPECT_NEAR(profile.Height(0.9 * 50.0, d, shallow), Interior(0.9) * d, 1e-7 * d);
}

TEST(FassettThomsonProfile, RimVariantsJoinAsPublished) {
    const double d = 10.0;
    const CraterState state = Fresh(0.2);
    const auto step = [&](const FassettThomsonProfile& profile, double x) {
        return profile.Height((x + 1e-6) * 0.5 * d, d, state) -
               profile.Height((x - 1e-6) * 0.5 * d, d, state);
    };
    const FassettThomsonProfile original(FassettThomsonRim::Original);
    EXPECT_NEAR(step(original, 0.98), 0.0, 2e-5 * d);
    const FassettThomsonProfile atOne(FassettThomsonRim::AtOneRadius);
    EXPECT_NEAR(atOne.Height(0.99 * 0.5 * d, d, state), Interior(0.99) * d, 1e-7 * d);
    EXPECT_NEAR(step(atOne, 1.0), (Exterior(1.0) - Interior(1.0)) * d, 1e-5);
    EXPECT_NEAR(step(atOne, 1.0) / d, -0.0076, 1e-4);
    const FassettThomsonProfile plateau(FassettThomsonRim::Plateau);
    EXPECT_NEAR(plateau.Height(0.5 * d, d, state), kLunarFreshRimHeightRatio * d, 1e-7 * d);
    EXPECT_NEAR(plateau.Height(0.7 * 0.5 * d, d, state), Interior(0.7) * d, 1e-7 * d);
    EXPECT_NEAR(plateau.Height(0.0, d, state), (kLunarFreshRimHeightRatio - 0.2) * d, 1e-7 * d);
    EXPECT_NEAR(step(plateau, 1.02) / d, -0.0042, 1e-4);
    double previous = original.Height(0.0, d, state);
    for (int i = 1; i <= 97; ++i) {
        const double h = original.Height(i * 0.01 * 0.5 * d, d, state);
        EXPECT_GE(h, previous - 1e-12) << i;
        previous = h;
    }
}

TEST(FassettThomsonProfile, ExteriorEasesToTheGroundAtItsReach) {
    const double d = 150.0;
    const double radius = 0.5 * d;
    const double step = 0.01;
    const CraterState state = Fresh(0.2);
    for (const auto rim : {FassettThomsonRim::Original, FassettThomsonRim::AtOneRadius,
                           FassettThomsonRim::Plateau}) {
        const FassettThomsonProfile profile(rim);
        double previous = 0.0;
        double largest = 0.0;
        for (double r = 1.03 * radius; r + step < 1.5 * radius; r += step) {
            const double slope =
                (profile.Height(r + step, d, state) - profile.Height(r, d, state)) / step;
            if (r > 1.03 * radius) {
                largest = std::max(largest, std::fabs(slope - previous));
            }
            previous = slope;
        }
        EXPECT_LT(largest, 2e-3) << "a slope break on the exterior";
        EXPECT_LT(std::fabs(previous), 2e-3) << "the exterior still slopes at its reach";
        EXPECT_GE(profile.Height(1.4 * radius, d, state), 0.0);
    }
}
