// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/noise/value.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

using terraingen::ValueNoise2;

namespace {

constexpr std::uint32_t kSeed = 99u;

constexpr float F(int v) {
    return static_cast<float>(v);
}

}  // namespace

TEST(ValueNoise2, StaysWithinUnitRangeAndUsesIt) {
    float lowest = 1.0f;
    float highest = -1.0f;
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 200; ++x) {
            const float n = ValueNoise2(F(x) * 0.13f, F(y) * 0.13f, kSeed);
            lowest = std::fmin(lowest, n);
            highest = std::fmax(highest, n);
        }
    }
    EXPECT_GE(lowest, -1.0f);
    EXPECT_LE(highest, 1.0f);
    EXPECT_LT(lowest, -0.8f);
    EXPECT_GT(highest, 0.8f);
}

TEST(ValueNoise2, InterpolatesLatticeValues) {
    const float corner = ValueNoise2(3.0f, -2.0f, kSeed);
    EXPECT_EQ(ValueNoise2(3, -2, 0.0f, 0.0f, kSeed), corner);
    EXPECT_NE(corner, 0.0f);
}

TEST(ValueNoise2, IsContinuousAcrossCellEdges) {
    constexpr float kEps = 1e-4f;
    for (int i = -10; i <= 10; ++i) {
        EXPECT_NEAR(ValueNoise2(F(i) - kEps, 0.37f, kSeed), ValueNoise2(F(i) + kEps, 0.37f, kSeed),
                    1e-3f);
        EXPECT_NEAR(ValueNoise2(0.61f, F(i) - kEps, kSeed), ValueNoise2(0.61f, F(i) + kEps, kSeed),
                    1e-3f);
    }
}

TEST(ValueNoise2, HasNearZeroMeanAndDependsOnSeed) {
    double sum = 0.0;
    int differing = 0;
    for (int i = 0; i < 20000; ++i) {
        const float x = F(i % 200) * 0.71f + 0.3f;
        const float y = F(i / 200) * 0.53f + 0.1f;
        sum += static_cast<double>(ValueNoise2(x, y, kSeed));
        differing += ValueNoise2(x, y, kSeed) != ValueNoise2(x, y, kSeed + 1u) ? 1 : 0;
    }
    EXPECT_NEAR(sum / 20000.0, 0.0, 0.05);
    EXPECT_GT(differing, 19900);
}
