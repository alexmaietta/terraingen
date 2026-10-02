#include "terraingen/kernels/philox.h"

#include <gtest/gtest.h>

#include <set>
#include <tuple>

using terraingen::Philox4x32;
using terraingen::U32x2;
using terraingen::U32x4;

TEST(Philox4x32, MatchesRandom123KnownAnswers) {
    EXPECT_EQ(Philox4x32(U32x4{0u, 0u, 0u, 0u}, U32x2{0u, 0u}),
              (U32x4{0x6627e8d5u, 0xe169c58du, 0xbc57ac4cu, 0x9b00dbd8u}));
    EXPECT_EQ(Philox4x32(U32x4{0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu},
                         U32x2{0xffffffffu, 0xffffffffu}),
              (U32x4{0x408f276du, 0x41c83b0eu, 0xa20bc7c6u, 0x6d5451fdu}));
    EXPECT_EQ(Philox4x32(U32x4{0x243f6a88u, 0x85a308d3u, 0x13198a2eu, 0x03707344u},
                         U32x2{0xa4093822u, 0x299f31d0u}),
              (U32x4{0xd16cfe09u, 0x94fdccebu, 0x5001e420u, 0x24126ea1u}));
}

TEST(Philox4x32, IsConstexpr) {
    constexpr U32x4 kOut = Philox4x32(U32x4{}, U32x2{});
    static_assert(kOut.x == 0x6627e8d5u);
    EXPECT_EQ(kOut.w, 0x9b00dbd8u);
}

TEST(Philox4x32, EveryCounterAndKeyWordMatters) {
    const U32x4 base = Philox4x32(U32x4{1u, 2u, 3u, 4u}, U32x2{5u, 6u});
    EXPECT_NE(Philox4x32(U32x4{0u, 2u, 3u, 4u}, U32x2{5u, 6u}), base);
    EXPECT_NE(Philox4x32(U32x4{1u, 0u, 3u, 4u}, U32x2{5u, 6u}), base);
    EXPECT_NE(Philox4x32(U32x4{1u, 2u, 0u, 4u}, U32x2{5u, 6u}), base);
    EXPECT_NE(Philox4x32(U32x4{1u, 2u, 3u, 0u}, U32x2{5u, 6u}), base);
    EXPECT_NE(Philox4x32(U32x4{1u, 2u, 3u, 4u}, U32x2{0u, 6u}), base);
    EXPECT_NE(Philox4x32(U32x4{1u, 2u, 3u, 4u}, U32x2{5u, 0u}), base);
}

TEST(Philox4x32, ProducesNoRepeatsOverConsecutiveCounters) {
    std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t>> seen;
    for (std::uint32_t i = 0; i < 50000u; ++i) {
        const U32x4 out = Philox4x32(U32x4{i, 0u, 0u, 0u}, U32x2{7u, 9u});
        seen.emplace(out.x, out.y, out.z, out.w);
    }
    EXPECT_EQ(seen.size(), 50000u);
}

TEST(U32x2, ComparesBothWords) {
    EXPECT_EQ((U32x2{1u, 2u}), (U32x2{1u, 2u}));
    EXPECT_NE((U32x2{1u, 2u}), (U32x2{0u, 2u}));
    EXPECT_NE((U32x2{1u, 2u}), (U32x2{1u, 0u}));
}

TEST(U32x4, ComparesEveryWord) {
    const U32x4 base{1u, 2u, 3u, 4u};
    EXPECT_EQ(base, (U32x4{1u, 2u, 3u, 4u}));
    EXPECT_NE(base, (U32x4{0u, 2u, 3u, 4u}));
    EXPECT_NE(base, (U32x4{1u, 0u, 3u, 4u}));
    EXPECT_NE(base, (U32x4{1u, 2u, 0u, 4u}));
    EXPECT_NE(base, (U32x4{1u, 2u, 3u, 0u}));
}
