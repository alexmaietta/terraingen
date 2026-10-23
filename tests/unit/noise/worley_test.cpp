// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/noise/worley.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

using terraingen::Worley2;
using terraingen::Worley3;

namespace {

constexpr std::uint32_t kSeed = 7u;

constexpr float F(int v) {
    return static_cast<float>(v);
}

/// The distance to the nearest feature point and its cell's value, by searching 9 x 9 cells.
struct Nearest {
    float distance = 1e30f;
    float value = 0.0f;
};

Nearest BruteForce2(float x, float y) {
    const float cellX = std::floor(x);
    const float cellY = std::floor(y);
    Nearest best;
    for (int dy = -4; dy <= 4; ++dy) {
        for (int dx = -4; dx <= 4; ++dx) {
            const auto cx = static_cast<std::int32_t>(cellX) + dx;
            const auto cy = static_cast<std::int32_t>(cellY) + dy;
            const terraingen::UVec3 h = terraingen::Pcg3dHash(terraingen::UVec3{
                static_cast<std::uint32_t>(cx), static_cast<std::uint32_t>(cy), kSeed});
            const float px = static_cast<float>(cx) + terraingen::HashToUnitFloat(h.y);
            const float py = static_cast<float>(cy) + terraingen::HashToUnitFloat(h.z);
            const float d = std::hypot(px - x, py - y);
            if (d < best.distance) {
                best = {d, 2.0f * terraingen::HashToUnitFloat(h.x) - 1.0f};
            }
        }
    }
    return best;
}

/// As `BruteForce2` in 3D, searching 7 x 7 x 7 cells.
Nearest BruteForce3(float x, float y, float z) {
    const terraingen::LatticePoint3 point = terraingen::SplitLattice3(x, y, z);
    const std::uint32_t seedKey = terraingen::PcgHash(kSeed);
    Nearest best;
    for (int dz = -3; dz <= 3; ++dz) {
        for (int dy = -3; dy <= 3; ++dy) {
            for (int dx = -3; dx <= 3; ++dx) {
                const std::int32_t cx = point.ix + dx;
                const std::int32_t cy = point.iy + dy;
                const std::int32_t cz = point.iz + dz;
                const terraingen::UVec3 h =
                    terraingen::Pcg3dHash(terraingen::detail::CellularLatticeKey3(
                        static_cast<std::uint32_t>(cx), static_cast<std::uint32_t>(cy),
                        static_cast<std::uint32_t>(cz), seedKey));
                const float px = static_cast<float>(cx) + terraingen::HashToUnitFloat(h.x) - x;
                const float py = static_cast<float>(cy) + terraingen::HashToUnitFloat(h.y) - y;
                const float pz = static_cast<float>(cz) + terraingen::HashToUnitFloat(h.z) - z;
                const float d = std::sqrt(px * px + py * py + pz * pz);
                if (d < best.distance) {
                    const std::uint32_t valueWord = terraingen::PcgHash(h.x ^ h.y ^ h.z);
                    best = {d, 2.0f * terraingen::HashToUnitFloat(valueWord) - 1.0f};
                }
            }
        }
    }
    return best;
}

}  // namespace

TEST(Worley2, F1AndTheCellValueMatchABruteForceSearch) {
    for (int i = 0; i < 500; ++i) {
        const float x = F(i % 25) * 0.83f - 9.0f;
        const float y = F(i / 25) * 0.67f + 3.0f;
        const auto d = Worley2(x, y, kSeed);
        const Nearest expected = BruteForce2(x, y);
        EXPECT_NEAR(d.f1, expected.distance, 1e-5f);
        EXPECT_EQ(d.value, expected.value) << x << ", " << y;
    }
}

TEST(Worley2, DistancesAreOrderedAndBounded) {
    for (int i = 0; i < 2000; ++i) {
        const auto d = Worley2(F(i % 50) * 0.37f, F(i / 50) * 0.41f, kSeed);
        EXPECT_GE(d.f1, 0.0f);
        EXPECT_LE(d.f1, d.f2);
        EXPECT_LE(d.f1, std::sqrt(2.0f));
        EXPECT_LE(d.f2, std::sqrt(5.0f));
        EXPECT_GE(d.value, -1.0f);
        EXPECT_LT(d.value, 1.0f);
    }
}

TEST(Worley2, IsContinuous) {
    constexpr float kEps = 1e-4f;
    for (int i = 0; i < 400; ++i) {
        const float x = F(i) * 0.173f - 30.0f;
        const auto a = Worley2(x, 2.5f, kSeed);
        const auto b = Worley2(x + kEps, 2.5f, kSeed);
        EXPECT_NEAR(a.f1, b.f1, 2e-4f);
        EXPECT_NEAR(a.f2, b.f2, 2e-4f);
    }
}

TEST(Worley2, DependsOnSeed) {
    EXPECT_NE(Worley2(1.3f, 2.7f, 1u).f1, Worley2(1.3f, 2.7f, 2u).f1);
}

TEST(Worley3, F1AndTheCellValueMatchABruteForceSearch) {
    for (int i = 0; i < 500; ++i) {
        const float x = F(i % 10) * 0.83f - 4.0f;
        const float y = F(i / 10 % 10) * 0.67f + 3.0f;
        const float z = F(i / 100) * 0.91f - 1.5f;
        const auto d = Worley3(x, y, z, kSeed);
        const Nearest expected = BruteForce3(x, y, z);
        EXPECT_NEAR(d.f1, expected.distance, 1e-5f);
        EXPECT_EQ(d.value, expected.value) << x << ", " << y << ", " << z;
    }
}

TEST(Worley3, DistancesAreOrderedAndBounded) {
    for (int i = 0; i < 2000; ++i) {
        const auto d =
            Worley3(F(i % 13) * 0.37f, F(i / 13 % 13) * 0.41f, F(i / 169) * 0.29f, kSeed);
        EXPECT_GE(d.f1, 0.0f);
        EXPECT_LE(d.f1, d.f2);
        EXPECT_LE(d.f1, std::sqrt(3.0f));
        EXPECT_LE(d.f2, std::sqrt(6.0f));
        EXPECT_GE(d.value, -1.0f);
        EXPECT_LT(d.value, 1.0f);
    }
}

TEST(Worley3, IsContinuous) {
    constexpr float kEps = 1e-4f;
    for (int i = 0; i < 400; ++i) {
        const float z = F(i) * 0.173f - 30.0f;
        const auto a = Worley3(0.5f, 2.5f, z, kSeed);
        const auto b = Worley3(0.5f, 2.5f, z + kEps, kSeed);
        EXPECT_NEAR(a.f1, b.f1, 2e-4f);
        EXPECT_NEAR(a.f2, b.f2, 2e-4f);
    }
}

TEST(Worley3, SplitsThePointLikeTheLatticeOverload) {
    const auto a = Worley3(1.3f, -2.6f, 7.2f, kSeed);
    const auto b = Worley3(terraingen::SplitLattice3(1.3f, -2.6f, 7.2f), kSeed);
    EXPECT_EQ(a.f1, b.f1);
    EXPECT_EQ(a.f2, b.f2);
    EXPECT_EQ(a.value, b.value);
}

TEST(Worley3, DependsOnSeed) {
    EXPECT_NE(Worley3(1.3f, 2.7f, 0.4f, 1u).f1, Worley3(1.3f, 2.7f, 0.4f, 2u).f1);
}
