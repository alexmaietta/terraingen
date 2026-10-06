#include "terraingen/noise/kernels/perlin.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

using terraingen::PerlinNoise2;

namespace {

constexpr std::uint32_t kSeed = 1234u;

constexpr float F(int v) {
    return static_cast<float>(v);
}

}  // namespace

TEST(PerlinNoise2, IsZeroAtLatticePoints) {
    for (int y = -5; y <= 5; ++y) {
        for (int x = -5; x <= 5; ++x) {
            EXPECT_EQ(PerlinNoise2(F(x), F(y), kSeed), 0.0f);
        }
    }
}

TEST(PerlinNoise2, StaysWithinUnitRange) {
    float maxAbs = 0.0f;
    for (int y = 0; y < 400; ++y) {
        for (int x = 0; x < 400; ++x) {
            const float n =
                PerlinNoise2(F(x) * 0.0625f + 0.03125f, F(y) * 0.0625f + 0.03125f, kSeed);
            maxAbs = std::max(maxAbs, std::fabs(n));
        }
    }
    EXPECT_LE(maxAbs, 1.0f + 1e-6f);
    EXPECT_GT(maxAbs, 0.5f);
}

TEST(PerlinNoise2, ReachesOneWhereAllGradientsPointAtTheCellCentre) {
    using terraingen::detail::PerlinCornerHash;
    for (std::uint32_t ix = 0; ix < 1000000u; ++ix) {
        const bool outward = (PerlinCornerHash(ix, 0u, kSeed) & 7u) == 4u &&
                             (PerlinCornerHash(ix + 1u, 0u, kSeed) & 7u) == 5u &&
                             (PerlinCornerHash(ix, 1u, kSeed) & 7u) == 6u &&
                             (PerlinCornerHash(ix + 1u, 1u, kSeed) & 7u) == 7u;
        if (outward) {
            EXPECT_NEAR(PerlinNoise2(static_cast<std::int32_t>(ix), 0, 0.5f, 0.5f, kSeed), 1.0f,
                        1e-6f);
            return;
        }
    }
    FAIL() << "No cell with all-outward gradients found";
}

TEST(PerlinNoise2, HasNearZeroMean) {
    double sum = 0.0;
    int count = 0;
    for (int y = 0; y < 300; ++y) {
        for (int x = 0; x < 300; ++x) {
            sum +=
                static_cast<double>(PerlinNoise2(F(x) * 0.37f + 0.1f, F(y) * 0.37f + 0.2f, kSeed));
            ++count;
        }
    }
    EXPECT_NEAR(sum / count, 0.0, 0.02);
}

TEST(PerlinNoise2, IsContinuousAcrossCellEdges) {
    constexpr float kEps = 1e-4f;
    for (int i = -20; i <= 20; ++i) {
        const float edge = static_cast<float>(i);
        for (const float t : {0.13f, 0.5f, 0.87f}) {
            EXPECT_NEAR(PerlinNoise2(edge - kEps, F(i) + t, kSeed),
                        PerlinNoise2(edge + kEps, F(i) + t, kSeed), 1e-3f);
            EXPECT_NEAR(PerlinNoise2(F(i) + t, edge - kEps, kSeed),
                        PerlinNoise2(F(i) + t, edge + kEps, kSeed), 1e-3f);
        }
    }
}

TEST(PerlinNoise2, CellOverloadMatchesFloatOverload) {
    EXPECT_EQ(PerlinNoise2(-3, 7, 0.25f, 0.75f, kSeed), PerlinNoise2(-2.75f, 7.75f, kSeed));
}

TEST(PerlinNoise2, IsDeterministicPerSeedAndVariesAcrossSeeds) {
    int differing = 0;
    for (int i = 0; i < 100; ++i) {
        const float x = F(i) * 0.731f + 0.2f;
        const float y = F(i) * 0.419f + 0.4f;
        EXPECT_EQ(PerlinNoise2(x, y, kSeed), PerlinNoise2(x, y, kSeed));
        if (PerlinNoise2(x, y, kSeed) != PerlinNoise2(x, y, kSeed + 1u)) {
            ++differing;
        }
    }
    EXPECT_GT(differing, 95);
}

TEST(PerlinNoise2, HandlesExtremeLatticeCells) {
    const float n = PerlinNoise2(INT32_MAX, INT32_MIN, 0.5f, 0.5f, kSeed);
    EXPECT_TRUE(std::isfinite(n));
    EXPECT_LE(std::fabs(n), 1.0f + 1e-6f);
}
