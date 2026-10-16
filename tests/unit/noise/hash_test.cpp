#include "terraingen/noise/hash.h"

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <set>

using terraingen::HashToUnitFloat;
using terraingen::Pcg3dHash;
using terraingen::PcgHash;
using terraingen::UVec3;

TEST(PcgHash, IsDeterministicAndConstexpr) {
    constexpr std::uint32_t kHash = PcgHash(42u);
    EXPECT_EQ(PcgHash(42u), kHash);
}

TEST(PcgHash, HasNoCollisionsOnConsecutiveInputs) {
    std::set<std::uint32_t> seen;
    for (std::uint32_t i = 0; i < 100000u; ++i) {
        seen.insert(PcgHash(i));
    }
    EXPECT_EQ(seen.size(), 100000u);
}

TEST(PcgHash, FlipsAboutHalfTheOutputBitsPerInputBit) {
    double totalFlips = 0.0;
    int trials = 0;
    for (std::uint32_t v = 0; v < 1000u; ++v) {
        for (std::uint32_t bit = 0; bit < 32u; ++bit) {
            totalFlips += std::popcount(PcgHash(v) ^ PcgHash(v ^ (1u << bit)));
            ++trials;
        }
    }
    EXPECT_NEAR(totalFlips / trials, 16.0, 1.0);
}

TEST(Pcg3dHash, DependsOnEveryLane) {
    const UVec3 base = Pcg3dHash(UVec3{1u, 2u, 3u});
    EXPECT_NE(Pcg3dHash(UVec3{0u, 2u, 3u}).x, base.x);
    EXPECT_NE(Pcg3dHash(UVec3{1u, 0u, 3u}).x, base.x);
    EXPECT_NE(Pcg3dHash(UVec3{1u, 2u, 0u}).x, base.x);
    EXPECT_NE(base.x, base.y);
    EXPECT_NE(base.y, base.z);
}

TEST(HashToUnitFloat, MapsToHalfOpenUnitInterval) {
    EXPECT_EQ(HashToUnitFloat(0u), 0.0f);
    EXPECT_LT(HashToUnitFloat(0xFFFFFFFFu), 1.0f);
    EXPECT_EQ(HashToUnitFloat(0xFFFFFFFFu), 1.0f - 0x1p-24f);
    EXPECT_EQ(HashToUnitFloat(0x80000000u), 0.5f);
}

TEST(UVec3, ComparesEveryLane) {
    const UVec3 v{1u, 2u, 3u};
    EXPECT_EQ(v, (UVec3{1u, 2u, 3u}));
    EXPECT_NE(v, (UVec3{0u, 2u, 3u}));
    EXPECT_NE(v, (UVec3{1u, 0u, 3u}));
    EXPECT_NE(v, (UVec3{1u, 2u, 0u}));
}
