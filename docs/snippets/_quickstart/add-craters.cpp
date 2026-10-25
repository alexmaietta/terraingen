// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Craters on a Flat Source for the Quickstart
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, QuickstartAddCraters) {
    // --8<-- [start:add_craters]
    TerrainGenerator terrain;
    terrain.source = std::make_shared<FlatSource>(257, 257, 0.5);  // 257 x 257 cells of 0.5 m
    terrain.layers = {std::make_shared<CraterLayer>()};
    const Raster<float> heights = Evaluate(terrain);  // meters
    // --8<-- [end:add_craters]
    EXPECT_EQ(heights.Width(), 257);
}

}  // namespace
}  // namespace terraingen
