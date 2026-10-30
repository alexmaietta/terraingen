// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Fill a Buffer You Own
///
/// A simulator or renderer often holds its own height buffer, such as a heightfield collider or a
/// texture staging buffer. `Fill` writes a grid's heights straight into it, row by row from the
/// top, with no copy, to the same bits `Evaluate` gives.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>
#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, FillABufferYouOwn) {
    // --8<-- [start:fill_a_buffer_you_own]
    const Terrain terrain(Surface{}, std::make_shared<FlatSource>(129, 129, 1.0),
                          {std::make_shared<CraterLayer>()});
    const RasterGrid grid = *terrain.Grid();
    std::vector<float> heightfield(grid.width * grid.height);  // the simulator's own buffer
    terrain.Fill(grid, heightfield);
    // --8<-- [end:fill_a_buffer_you_own]
    const Raster<float> evaluated = terrain.Evaluate(grid);
    EXPECT_TRUE(std::equal(heightfield.begin(), heightfield.end(), evaluated.Values().begin()));
}

}  // namespace
}  // namespace terraingen
