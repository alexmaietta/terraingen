// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Add Craters to a Terrain
///
/// A crater layer with the default models draws craters from the lunar production function,
/// degraded by their ages, into the heights beneath it. Here it lies on a flat 257 x 257 source
/// of 1 m cells.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, AddCratersToATerrain) {
    // --8<-- [start:add_craters_to_a_terrain]
    const auto source = std::make_shared<FlatSource>(257, 257, 1.0);
    const Terrain terrain(Surface{}, source, {std::make_shared<CraterLayer>()});
    const Raster<float> heights = terrain.Evaluate();  // meters
    // --8<-- [end:add_craters_to_a_terrain]
    EXPECT_EQ(heights.Width(), 257);
}

}  // namespace
}  // namespace terraingen
