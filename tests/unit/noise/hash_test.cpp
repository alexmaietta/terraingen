#include "terraingen/noise/hash.h"

#include <gtest/gtest.h>

#include <bitset>
#include <cstddef>
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
            totalFlips +=
                static_cast<double>(std::bitset<32>(PcgHash(v) ^ PcgHash(v ^ (1u << bit))).count());
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

TEST(HashWord64, MixesBothWordsAndTheSeed) {
    constexpr std::uint32_t kKey = terraingen::HashWord64(0x0000000100000002ull, 3u);
    EXPECT_EQ(kKey, PcgHash(2u ^ PcgHash(1u ^ 3u)));
    EXPECT_NE(terraingen::HashWord64(0x0000000100000002ull, 4u), kKey);
    EXPECT_NE(terraingen::HashWord64(0x0000000200000002ull, 3u), kKey);
}

TEST(HashWords, SpreadsSmallKeysAndDependsOnOrder) {
    std::set<std::size_t> seen;
    for (std::uint64_t i = 0; i < 64; ++i) {
        for (std::uint64_t j = 0; j < 64; ++j) {
            seen.insert(terraingen::HashWords({i, j}));
        }
    }
    EXPECT_EQ(seen.size(), 64u * 64u);
    EXPECT_NE(terraingen::HashWords({1u, 2u}), terraingen::HashWords({2u, 1u}));
    EXPECT_NE(terraingen::HashWords({1u}), terraingen::HashWords({1u, 0u}));
}
