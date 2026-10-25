// SPDX-License-Identifier: BSD-3-Clause

#include "albedo/bands.h"

#include "terraingen/albedo/maturity.h"
#include "terraingen/layers/fade.h"
#include "terraingen/util/bits.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

using terraingen::AlbedoSample;

TEST(ScaleBands, ScalesTheBandsInUseAndKeepsNaN) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    AlbedoSample value{0.1f, nan, 0.3f};
    terraingen::ScaleBands(value, 2, 2.0);
    EXPECT_FLOAT_EQ(value[0], 0.2f);
    EXPECT_TRUE(std::isnan(value[1]));
    EXPECT_FLOAT_EQ(value[2], 0.3f) << "bands past the count stay";
}

TEST(ChangeBands, AppliesARelativeChangeNeverBelowZero) {
    AlbedoSample value{0.2f, 0.4f, 0.0f};
    terraingen::ChangeBands(value, 2, 0.5);
    EXPECT_FLOAT_EQ(value[0], 0.3f);
    EXPECT_FLOAT_EQ(value[1], 0.6f);
    terraingen::ChangeBands(value, 2, -2.0);
    EXPECT_EQ(value[0], 0.0f);
    AlbedoSample same{0.2f, 0.0f, 0.0f};
    terraingen::ChangeBands(same, 1, 0.0);
    EXPECT_EQ(same[0], 0.2f);
}

TEST(MatureBands, MovesEachBandToTheMaturityThenScales) {
    AlbedoSample value{0.2f, std::numeric_limits<float>::quiet_NaN(), 0.0f};
    terraingen::MatureBands(value, 2, 0.2, 0.3, 0.08, 1.5);
    EXPECT_FLOAT_EQ(value[0],
                    static_cast<float>(1.5 * terraingen::ReflectanceAtMaturity(
                                                 static_cast<double>(0.2f), 0.2, 0.3, 0.08)));
    EXPECT_TRUE(std::isnan(value[1]));
}

TEST(SoftEdge, FadesOverTheLastFeatherOfTheWay) {
    EXPECT_EQ(terraingen::SoftEdge(0.5, 0.3), 1.0);
    EXPECT_EQ(terraingen::SoftEdge(1.0, 0.3), 0.0);
    EXPECT_DOUBLE_EQ(terraingen::SoftEdge(0.85, 0.3), 0.5);
    EXPECT_DOUBLE_EQ(terraingen::FadeOut(0.5, 0.3, 0.4), 0.5);
    EXPECT_DOUBLE_EQ(terraingen::SmoothRamp(0.5, 0.3, 0.4), 0.5);
    EXPECT_EQ(terraingen::SmoothRamp(0.2, 0.3, 0.4), 0.0);
}

TEST(RingNoise, IsPeriodicAroundTheRing) {
    const double here = terraingen::RingNoise(2.0, 0.3, 0.5, 7u, 3);
    EXPECT_DOUBLE_EQ(terraingen::RingNoise(2.0, 0.3 + 2.0 * terraingen::kPi, 0.5, 7u, 3), here);
    EXPECT_LE(std::fabs(here), 1.0);
    EXPECT_NE(terraingen::RingNoise(2.0, 0.3, 0.5, 8u, 3), here);
}

TEST(EdgeLobe, PullsTheEdgeInWhereTheRingNoiseIsLow) {
    EXPECT_EQ(terraingen::EdgeLobe(0.0, 2.0, 0.3, 0.0, 7u, 3), 1.0);
    for (double angle = 0.0; angle < 6.0; angle += 0.25) {
        const double noise = terraingen::RingNoise(2.0, angle, 0.5, 7u, 3);
        EXPECT_DOUBLE_EQ(terraingen::EdgeLobe(0.2, 2.0, angle, 0.5, 7u, 3),
                         1.0 - 0.2 * std::clamp(0.5 - noise, 0.0, 1.0));
    }
}

TEST(ValidateRelativeChange, NeedsChangesAboveMinusOneAndBounded) {
    using terraingen::ValueDistribution;
    EXPECT_NO_THROW(terraingen::ValidateRelativeChange(
        ValueDistribution{ValueDistribution::Kind::Uniform, -0.5, 0.5}, "A change"));
    try {
        terraingen::ValidateRelativeChange(
            ValueDistribution{ValueDistribution::Kind::Constant, -1.0}, "A change");
        FAIL() << "a change of -1 accepted";
    } catch (const std::invalid_argument& error) {
        EXPECT_STREQ(error.what(), "A change must be above -1 and bounded");
    }
}

TEST(LifeLeft, FallsFromOneToZeroOverTheLifetime) {
    EXPECT_EQ(terraingen::LifeLeft(0.0, 10.0), 1.0);
    EXPECT_DOUBLE_EQ(terraingen::LifeLeft(0.005, 10.0), 0.5);
    EXPECT_LT(terraingen::LifeLeft(0.02, 10.0), 0.0);
}

TEST(ChangeOverlay, LetsEachLayerCoverWhatTheOnesAboveLeaveShowing) {
    terraingen::ChangeOverlay overlay;
    EXPECT_EQ(overlay.Change(), 0.0);
    overlay.Add(0.5, 0.2);
    overlay.Add(1.0, -0.1);
    overlay.Add(1.0, 0.4);
    EXPECT_DOUBLE_EQ(overlay.Change(), 0.5 * 0.2 + 0.5 * -0.1);
}
