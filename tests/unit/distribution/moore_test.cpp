// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/distribution/moore.h"

#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>

using terraingen::MooreDistribution;
using terraingen::MooreTerrain;

TEST(MooreDistribution, FollowsEachTerrainsSegments) {
    const auto rough = MooreDistribution(MooreTerrain::RoughMare);
    EXPECT_DOUBLE_EQ(rough.Cumulative(10.0), 0.1 / 100.0);
    EXPECT_DOUBLE_EQ(rough.Cumulative(200.0), 10.0 / 8e6);
    const auto smooth = MooreDistribution(MooreTerrain::SmoothMare);
    EXPECT_DOUBLE_EQ(smooth.Cumulative(150.0), std::pow(10.0, -2.038) * std::pow(150.0, -1.68));
    const auto uplands = MooreDistribution(MooreTerrain::Uplands);
    EXPECT_DOUBLE_EQ(uplands.Cumulative(2000.0), 100.0 / 8e9);
    const auto heavy = MooreDistribution(MooreTerrain::HeavilyCratered);
    EXPECT_DOUBLE_EQ(heavy.Cumulative(100.0), 0.1 / 1e4);
    EXPECT_THROW(heavy.Cumulative(20000.0), std::domain_error) << "beyond 10 km";
}
