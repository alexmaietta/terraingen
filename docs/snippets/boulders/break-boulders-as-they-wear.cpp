// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Break Boulders Into Pieces as They Wear
///
/// With fragmentation, boulders above a size crack into pieces once their wear passes a threshold,
/// the pieces still fitting together, and from a higher wear they scatter as rubble. Each piece
/// replaces its parent in the list and names it, so pieces of one boulder can be found together.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <cstddef>
#include <cstdint>

namespace terraingen {
namespace {

TEST_F(SnippetTest, BreakBouldersAsTheyWear) {
    BoulderLayerOptions options;
    options.seed = 4;
    options.sizes.distribution = nullptr;
    options.sizes.coverage = 0.1;
    options.sizes.minDiameter = 0.3;

    // --8<-- [start:break_boulders_as_they_wear]
    BoulderFragmentation fragmentation;
    fragmentation.minDiameter = 0.6;  // meters; smaller boulders stay whole
    fragmentation.splitWear = 0.3;    // cracked into pieces that still fit from here
    fragmentation.rubbleWear = 0.8;   // and scattered as rubble from here
    fragmentation.maxPieces = 6;
    options.fragmentation = fragmentation;
    const BoulderList list = BoulderLayer(options).Boulders({-15.0, -15.0, 15.0, 15.0});
    std::size_t pieces = 0;
    for (const std::uint64_t parent : list.Parents()) {
        pieces += parent != 0;  // a piece names the boulder it broke from
    }
    // --8<-- [end:break_boulders_as_they_wear]
    EXPECT_GT(pieces, 0u);
}

}  // namespace
}  // namespace terraingen
