// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Split a Boulder Shape Into Pieces
///
/// Any boulder shape splits into pieces that fit back together along near-vertical cracks, which
/// run out from the middle like slices of a pie or right through it like slabs. Each piece comes in
/// unit form with the center and scale that put it back where it was in its parent.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, SplitABoulderShapeIntoPieces) {
    BoulderShapeOptions options;
    options.styles.erase(std::remove_if(options.styles.begin(), options.styles.end(),
                                        [](const BoulderStyle& s) { return s.name != "block"; }),
                         options.styles.end());

    // --8<-- [start:split_a_boulder_shape_into_pieces]
    const BoulderShape parent = GenerateBoulderShape(5, options);
    const std::vector<BoulderFragment> pieces =
        FragmentBoulderShape(parent, 5, 2, BoulderCracks::Radial);
    std::vector<Vec3d> placed;  // each piece's points back where they were in the parent
    for (const BoulderFragment& piece : pieces) {
        for (const Vec3d& point : piece.shape.mesh.positions) {
            placed.push_back(piece.center + point * piece.scale);
        }
    }
    // --8<-- [end:split_a_boulder_shape_into_pieces]
    EXPECT_EQ(pieces.size(), 5u);
    EXPECT_FALSE(placed.empty());
}

}  // namespace
}  // namespace terraingen
