#include "terraingen/noise/kernels/simplex.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

using terraingen::SimplexNoise2;

namespace {

constexpr std::uint32_t kSeed = 4321u;

constexpr float F(int v) {
    return static_cast<float>(v);
}

}  // namespace

TEST(SimplexNoise2, StaysWithinUnitRangeAndUsesMostOfIt) {
    float maxAbs = 0.0f;
    for (int y = 0; y < 500; ++y) {
        for (int x = 0; x < 500; ++x) {
            maxAbs =
                std::fmax(maxAbs, std::fabs(SimplexNoise2(F(x) * 0.037f, F(y) * 0.041f, kSeed)));
        }
    }
    EXPECT_LE(maxAbs, 1.0f);
    EXPECT_GT(maxAbs, 0.6f);
}

TEST(SimplexNoise2, IsContinuous) {
    constexpr float kEps = 1e-4f;
    for (int i = 0; i < 400; ++i) {
        const float x = F(i) * 0.173f - 30.0f;
        const float y = F(i) * 0.091f + 7.0f;
        EXPECT_NEAR(SimplexNoise2(x, y, kSeed), SimplexNoise2(x + kEps, y, kSeed), 2e-3f);
        EXPECT_NEAR(SimplexNoise2(x, y, kSeed), SimplexNoise2(x, y + kEps, kSeed), 2e-3f);
    }
}

TEST(SimplexNoise2, HasNearZeroMeanAndDependsOnSeed) {
    double sum = 0.0;
    int differing = 0;
    for (int i = 0; i < 20000; ++i) {
        const float x = F(i % 200) * 0.71f + 0.3f;
        const float y = F(i / 200) * 0.53f + 0.1f;
        sum += static_cast<double>(SimplexNoise2(x, y, kSeed));
        differing += SimplexNoise2(x, y, kSeed) != SimplexNoise2(x, y, kSeed + 1u) ? 1 : 0;
    }
    EXPECT_NEAR(sum / 20000.0, 0.0, 0.03);
    EXPECT_GT(differing, 19500);
}

TEST(SimplexNoise2, IsZeroAtLatticeOrigin) {
    EXPECT_EQ(SimplexNoise2(0.0f, 0.0f, kSeed), 0.0f);
}
