// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/analysis/raster_stats.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using terraingen::ComputeStats;
using terraingen::Raster;

TEST(ComputeStats, SummarizesAllCells) {
    Raster<float> raster(2, 2);
    raster(0, 0) = -1.0f;
    raster(1, 0) = 3.0f;
    raster(0, 1) = 2.0f;
    raster(1, 1) = 0.0f;
    const auto stats = ComputeStats(raster);
    EXPECT_EQ(stats.validCount, 4u);
    EXPECT_DOUBLE_EQ(stats.min, -1.0);
    EXPECT_DOUBLE_EQ(stats.max, 3.0);
    EXPECT_DOUBLE_EQ(stats.mean, 1.0);
    EXPECT_DOUBLE_EQ(stats.stddev, std::sqrt(2.5));
}

TEST(ComputeStats, SkipsNoDataAndNonFiniteCells) {
    Raster<float> raster(4, 1, 5.0f);
    raster.SetNoData(-9999.0f);
    raster(0, 0) = -9999.0f;
    raster(1, 0) = std::numeric_limits<float>::quiet_NaN();
    raster(2, 0) = std::numeric_limits<float>::infinity();
    const auto stats = ComputeStats(raster);
    EXPECT_EQ(stats.validCount, 1u);
    EXPECT_DOUBLE_EQ(stats.min, 5.0);
    EXPECT_DOUBLE_EQ(stats.max, 5.0);
    EXPECT_DOUBLE_EQ(stats.mean, 5.0);
}

TEST(ComputeStats, IsNaNWithoutValidCells) {
    const auto stats = ComputeStats(Raster<float>());
    EXPECT_EQ(stats.validCount, 0u);
    EXPECT_TRUE(std::isnan(stats.min));
    EXPECT_TRUE(std::isnan(stats.max));
    EXPECT_TRUE(std::isnan(stats.mean));
}

TEST(ComputeStats, WorksForIntegerRasters) {
    Raster<int> raster(3, 1);
    raster(0, 0) = 1;
    raster(1, 0) = 2;
    raster(2, 0) = 6;
    EXPECT_DOUBLE_EQ(ComputeStats(raster).mean, 3.0);
}

TEST(ComputeStats, SkipsNoDataInIntegerRasters) {
    Raster<int> raster(2, 1, -1);
    raster.SetNoData(-1);
    EXPECT_EQ(ComputeStats(raster).validCount, 0u);
    raster(1, 0) = 4;
    EXPECT_DOUBLE_EQ(ComputeStats(raster).mean, 4.0);
}
