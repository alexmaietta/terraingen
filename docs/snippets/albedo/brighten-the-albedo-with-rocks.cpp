// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Brighten the Albedo With Rocks
///
/// Rock is brighter than mature regolith, 2.5 times by default. `RockAlbedo` brightens each
/// sample by the share of it the boulders of a boulder layer cover, read on the ground beneath
/// that layer. Rocks too small to resolve add a speckle with the mean and variance of the cover
/// they would give.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, BrightenTheAlbedoWithRocks) {
    BoulderLayerOptions boulderOptions;
    boulderOptions.seed = 4;
    boulderOptions.sizes.distribution =
        std::make_shared<TabulatedSfd>(Sp8023IntercraterBlocks(SpecificationCurve::Maximum));
    const auto boulders = std::make_shared<BoulderLayer>(boulderOptions);
    TerrainGenerator terrain;
    terrain.source = std::make_shared<FlatSource>(401, 401, 0.1);
    terrain.layers = {boulders};
    // --8<-- [start:brighten_the_albedo_with_rocks]
    RockAlbedoOptions options;
    options.seed = 1;
    options.reflectanceRatio = 2.5;
    RockSource rocks;
    rocks.layer = boulders;
    rocks.ground = SharedGround(terrain, 0);  // beneath the boulder layer, layer 0
    Albedo albedo;
    albedo.source = std::make_shared<SyntheticAlbedo>(SyntheticAlbedoOptions{});
    albedo.layers = {std::make_shared<RockAlbedo>(options, std::vector<RockSource>{rocks})};
    const RasterGrid grid = RasterGrid::FromRaster(Evaluate(terrain));
    const std::vector<Raster<float>> bands = EvaluateAlbedo(albedo, grid, {});
    // --8<-- [end:brighten_the_albedo_with_rocks]
    ASSERT_EQ(bands.size(), 1u);
    const Span<const float> values = bands[0].Values();
    EXPECT_GT(*std::max_element(values.begin(), values.end()), 0.25f);
}

}  // namespace
}  // namespace terraingen
