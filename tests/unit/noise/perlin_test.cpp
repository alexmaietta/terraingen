// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/noise/perlin.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

using terraingen::PerlinNoise2;
using terraingen::PerlinNoise3;

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

TEST(PerlinNoise2, ReachesOneWhereAllGradientsPointAtTheCellCenter) {
    using terraingen::detail::PerlinCornerHash2;
    for (std::uint32_t ix = 0; ix < 1000000u; ++ix) {
        const bool outward = (PerlinCornerHash2(ix, 0u, kSeed) & 7u) == 4u &&
                             (PerlinCornerHash2(ix + 1u, 0u, kSeed) & 7u) == 5u &&
                             (PerlinCornerHash2(ix, 1u, kSeed) & 7u) == 6u &&
                             (PerlinCornerHash2(ix + 1u, 1u, kSeed) & 7u) == 7u;
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

TEST(PerlinNoise3, IsZeroAtLatticePointsAndStaysInRange) {
    for (int z = -3; z <= 3; ++z) {
        for (int y = -3; y <= 3; ++y) {
            for (int x = -3; x <= 3; ++x) {
                EXPECT_EQ(PerlinNoise3(F(x), F(y), F(z), kSeed), 0.0f);
            }
        }
    }
    float maxAbs = 0.0f;
    double sum = 0.0;
    int count = 0;
    for (int z = 0; z < 60; ++z) {
        for (int y = 0; y < 60; ++y) {
            for (int x = 0; x < 60; ++x) {
                const float n = PerlinNoise3(F(x) * 0.23f + 0.05f, F(y) * 0.29f + 0.11f,
                                             F(z) * 0.31f + 0.07f, kSeed);
                maxAbs = std::max(maxAbs, std::fabs(n));
                sum += static_cast<double>(n);
                ++count;
            }
        }
    }
    EXPECT_LE(maxAbs, 1.0f);
    EXPECT_GT(maxAbs, 0.5f);
    EXPECT_NEAR(sum / count, 0.0, 0.02);
}

TEST(PerlinNoise3, IsContinuousAcrossCellFacesAndDependsOnTheSeed) {
    constexpr float kEps = 1e-4f;
    for (int i = -10; i <= 10; ++i) {
        const float face = static_cast<float>(i);
        for (const float t : {0.13f, 0.5f, 0.87f}) {
            EXPECT_NEAR(PerlinNoise3(face - kEps, t, 0.3f, kSeed),
                        PerlinNoise3(face + kEps, t, 0.3f, kSeed), 1e-3f);
            EXPECT_NEAR(PerlinNoise3(t, face - kEps, 0.3f, kSeed),
                        PerlinNoise3(t, face + kEps, 0.3f, kSeed), 1e-3f);
            EXPECT_NEAR(PerlinNoise3(0.3f, t, face - kEps, kSeed),
                        PerlinNoise3(0.3f, t, face + kEps, kSeed), 1e-3f);
        }
    }
    EXPECT_NE(PerlinNoise3(0.5f, 0.25f, 0.75f, 1u), PerlinNoise3(0.5f, 0.25f, 0.75f, 2u));
    EXPECT_EQ(PerlinNoise3(-1.5f, 2.25f, -0.75f, kSeed),
              PerlinNoise3({-2, 2, -1, 0.5f, 0.25f, 0.25f}, kSeed));
}

TEST(PerlinCorner3, ReadsTheCornerFromItsBits) {
    using terraingen::detail::PerlinCorner3;
    using terraingen::detail::PerlinCornerHash3;
    using terraingen::detail::PerlinGradient3;
    const terraingen::LatticePoint3 point = {4, -3, 2, 0.25f, 0.5f, 0.75f};
    EXPECT_EQ(PerlinCorner3(point, 0u, kSeed),
              PerlinGradient3(PerlinCornerHash3(4u, static_cast<std::uint32_t>(-3), 2u, kSeed),
                              0.25f, 0.5f, 0.75f));
    EXPECT_EQ(PerlinCorner3(point, 5u, kSeed),
              PerlinGradient3(PerlinCornerHash3(5u, static_cast<std::uint32_t>(-3), 3u, kSeed),
                              -0.75f, 0.5f, -0.25f));
    EXPECT_EQ(PerlinCorner3(point, 6u, kSeed),
              PerlinGradient3(PerlinCornerHash3(4u, static_cast<std::uint32_t>(-2), 3u, kSeed),
                              0.25f, -0.5f, -0.25f));
}

TEST(PerlinGradient3, UsesTheTwelveCubeEdgeVectors) {
    using terraingen::detail::PerlinGradient3;
    EXPECT_EQ(PerlinGradient3(0u, 1.0f, 2.0f, 4.0f), 3.0f);
    EXPECT_EQ(PerlinGradient3(3u, 1.0f, 2.0f, 4.0f), -3.0f);
    EXPECT_EQ(PerlinGradient3(4u, 1.0f, 2.0f, 4.0f), 5.0f);
    EXPECT_EQ(PerlinGradient3(8u, 1.0f, 2.0f, 4.0f), 6.0f);
    EXPECT_EQ(PerlinGradient3(12u, 1.0f, 2.0f, 4.0f), 3.0f);
    EXPECT_EQ(PerlinGradient3(14u, 1.0f, 2.0f, 4.0f), 1.0f);
    EXPECT_EQ(PerlinGradient3(16u, 1.0f, 2.0f, 4.0f), PerlinGradient3(0u, 1.0f, 2.0f, 4.0f));
}

TEST(PerlinNoise2Precise, SplitsTheCellBeforeNarrowingToFloat) {
    EXPECT_EQ(terraingen::PerlinNoise2Precise(3.25, -1.5, 7u), PerlinNoise2(3.25f, -1.5f, 7u));
    const double far = 4294967296.0 + 12.375;
    EXPECT_EQ(terraingen::PerlinNoise2Precise(far, 0.5, 7u), PerlinNoise2(12, 0, 0.375f, 0.5f, 7u))
        << "cells wrap modulo 2^32 and the fraction stays exact";
}
