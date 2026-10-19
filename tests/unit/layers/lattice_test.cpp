#include "terraingen/layers/lattice.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>
#include <vector>

TEST(ForEachLatticeCell, VisitsTheRingsRowByRow) {
    std::vector<std::pair<std::int64_t, std::int64_t>> cells;
    terraingen::ForEachLatticeCell({-0.5, 7.0}, {1.0, 2.0}, 1, [&](std::int64_t i, std::int64_t j) {
        cells.emplace_back(i, j);
    });
    const std::vector<std::pair<std::int64_t, std::int64_t>> expected{
        {-2, 2}, {-1, 2}, {0, 2}, {-2, 3}, {-1, 3}, {0, 3}, {-2, 4}, {-1, 4}, {0, 4}};
    EXPECT_EQ(cells, expected);
    int count = 0;
    terraingen::ForEachLatticeCell({0.0, 0.0}, {1.0, 1.0}, 0,
                                   [&](std::int64_t, std::int64_t) { ++count; });
    EXPECT_EQ(count, 1);
}

TEST(LatticeStream, KeysEachCellByAllItsBits) {
    const auto first = [](std::int64_t i, std::int64_t j) {
        return terraingen::LatticeStream(3u, 0x1234u, i, j).NextU32();
    };
    EXPECT_EQ(first(5, -7), first(5, -7));
    EXPECT_NE(first(5, -7), first(-7, 5));
    EXPECT_NE(first(5, 7), first(5 + (std::int64_t{1} << 32), 7)) << "the high words count";
    terraingen::RandomStream direct(3u, 0x1234u, 5u, static_cast<std::uint32_t>(-7), 0xffffu << 16);
    EXPECT_EQ(direct.NextU32(), first(5, -7)) << "the key the layers have always used";
}
