// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Design a Boulder Style of Your Own
///
/// A boulder style names the solid a shape starts from and the ranges its axes, fractures, noise
/// and worn edges are drawn from. Here thin slabs are cut from a cube. Shapes drawn with only this
/// style are all slabs, while adding it to the default styles mixes it in by its weight.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <cstdint>
#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, DesignABoulderStyle) {
    // --8<-- [start:design_a_boulder_style]
    BoulderStyle style;
    style.name = "slab";
    style.solid = BaseSolid::Cube;
    style.intermediate = {0.8, 0.95};  // over the long axis
    style.shortAxis = {0.15, 0.25};    // thin and flat
    style.minFractures = 2;
    style.maxFractures = 4;
    style.edgeSoftness = {0.01, 0.03};
    BoulderShapeOptions options;
    options.styles = {style};  // or add it to DefaultBoulderStyles()
    options.subdivisions = 4;
    std::vector<BoulderShape> shapes;
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
        shapes.push_back(GenerateBoulderShape(seed, options));
    }
    // --8<-- [end:design_a_boulder_style]
    for (const BoulderShape& shape : shapes) {
        EXPECT_LE(shape.shortAxis, 0.25 + 1e-6);
        EXPECT_EQ(shape.style, 0u);
    }
}

}  // namespace
}  // namespace terraingen
