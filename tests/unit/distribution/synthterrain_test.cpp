// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/distribution/synthterrain.h"

#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>

TEST(SynthterrainBoulderDistribution, MatchesSynthterrainsPowerLaws) {
    using terraingen::SynthterrainBoulders;
    EXPECT_DOUBLE_EQ(terraingen::SynthterrainBoulderDistribution(SynthterrainBoulders::InterCrater)
                         .Cumulative(2.0),
                     0.0001 * std::pow(2.0, -1.75457));
    EXPECT_DOUBLE_EQ(
        terraingen::SynthterrainBoulderDistribution(SynthterrainBoulders::Haworth).Cumulative(2.0),
        0.002 * std::pow(2.0, -2.6607));
    EXPECT_DOUBLE_EQ(
        terraingen::SynthterrainBoulderDistribution(SynthterrainBoulders::ViperSpecification)
            .Cumulative(2.0),
        0.0003 * std::pow(2.0, -2.482));
    EXPECT_THROW(terraingen::SynthterrainBoulderDistribution(static_cast<SynthterrainBoulders>(9)),
                 std::invalid_argument);
}
