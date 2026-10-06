#include "terraingen/layers/craters/cell_cache.h"

#include <gtest/gtest.h>

#include <stdexcept>

using terraingen::CellIndex;
using terraingen::CraterCellCache;
using terraingen::CraterList;

namespace {

/// `count` craters tagged with the cell they came from.
CraterList Cell(int i, std::size_t count) {
    CraterList craters;
    for (std::size_t n = 0; n < count; ++n) {
        craters.Append({static_cast<double>(i), 0.0, 1.0, 0.0, n});
    }
    return craters;
}

}  // namespace

TEST(CraterCellCache, GeneratesEachCellOnceAndReturnsTheHeldCraters) {
    CraterCellCache cache(1000);
    int calls = 0;
    const auto generate = [&] {
        ++calls;
        return Cell(3, 5);
    };
    const auto first = cache.Get(2, CellIndex{3, 4}, generate);
    const auto again = cache.Get(2, CellIndex{3, 4}, generate);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(first, again);
    EXPECT_EQ(first->Size(), 5u);
    EXPECT_EQ(cache.Size(), 5u);
    cache.Get(3, CellIndex{3, 4}, generate);
    EXPECT_EQ(calls, 2) << "another band is another cell";
    EXPECT_EQ(cache.MaxCraters(), 1000u);
    EXPECT_EQ(cache.Evictions(), 0u);
}

TEST(CraterCellCache, StaysWithinItsBudgetByEvictingTheLeastRecentlyUsed) {
    CraterCellCache cache(160);
    for (int i = 0; i < 400; ++i) {
        cache.Get(0, CellIndex{i, 0}, [&] { return Cell(i, 4); });
    }
    EXPECT_LE(cache.Size(), 160u);
    EXPECT_GT(cache.Evictions(), 0u);
    int calls = 0;
    const auto craters = cache.Get(0, CellIndex{0, 0}, [&] {
        ++calls;
        return Cell(0, 4);
    });
    EXPECT_EQ(calls, 1) << "an evicted cell is generated again";
    EXPECT_EQ(craters->At(0).x, 0.0);
    EXPECT_THROW(CraterCellCache(0), std::invalid_argument);
}
