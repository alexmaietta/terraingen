// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Replace a Crater a DEM Holds Blurred
///
/// A DEM's posts soften the craters it holds: rims round off and bowls fill in between posts. A
/// manual crater with replace set swaps whatever the terrain holds there for a sharp crater of
/// its diameter and depth, cut into a plane fitted on a ring just beyond its reach, so neither the
/// old bowl nor its rim pulls the new crater down. Here a DEM with 4 m posts holds a 40 m crater
/// only 2 m deep, and a replacing crater makes it 8 m deep.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, ReplaceABlurredCrater) {
    Raster<float> posts(101, 101);
    posts.SetTransform(GeoTransform::NorthUp(-202.0, 202.0, 4.0));
    Crater worn = ManualCrater(0.0, 0.0, 40.0);
    worn.depth = 2.0;
    CraterLayer::Manual({worn})->Apply(posts, {4.0});
    const auto dem = std::make_shared<RasterSource>(posts);  // a 40 m crater only 2 m deep
    // --8<-- [start:replace_a_blurred_crater]
    Crater sharp = ManualCrater(0.0, 0.0, 40.0);
    sharp.depth = 8.0;
    sharp.replace = true;  // swaps out what the DEM holds there
    const auto craters = CraterLayer::Manual({sharp}, DefaultCraterModels(), DefaultCraterApply(),
                                             TerrainBase{Surface::Plane(), dem});
    const Terrain terrain(Surface::Plane(), dem, {craters});
    const Raster<float> heights =
        terrain.Evaluate(RasterGrid::Centered({0.0, 0.0}, {100.0, 100.0}, 0.5));
    // --8<-- [end:replace_a_blurred_crater]
    float low = heights(100, 100);
    float high = low;
    for (std::size_t column = 0; column < heights.Width(); ++column) {
        low = std::min(low, heights(column, 100));
        high = std::max(high, heights(column, 100));
    }
    EXPECT_NEAR(high - low, 8.0f, 1.0f);
}

}  // namespace
}  // namespace terraingen
