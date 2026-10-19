#include "terraingen/albedo/bands.h"

#include "terraingen/albedo/maturity.h"
#include "terraingen/layers/fade.h"
#include "terraingen/util/bits.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
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

TEST(LobeFactor, PullsTheEdgeInWhereTheNoiseIsLow) {
    EXPECT_EQ(terraingen::LobeFactor(0.6, 0.2), 1.0);
    EXPECT_DOUBLE_EQ(terraingen::LobeFactor(0.0, 0.2), 0.9);
    EXPECT_DOUBLE_EQ(terraingen::LobeFactor(-1.0, 0.2), 0.8);
}

TEST(LifeLeft, FallsFromOneToZeroOverTheLifetime) {
    EXPECT_EQ(terraingen::LifeLeft(0.0, 10.0), 1.0);
    EXPECT_DOUBLE_EQ(terraingen::LifeLeft(0.005, 10.0), 0.5);
    EXPECT_LT(terraingen::LifeLeft(0.02, 10.0), 0.0);
}
