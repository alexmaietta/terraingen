// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Throw Blocks Out Around Craters
///
/// Craters large enough to dig through the regolith throw out blocks, added to the boulders
/// between craters. The blocks are densest at the rim, thin out to a reach in crater radii and wear
/// away as the crater ages. A boulder layer takes the crater layer whose craters throw them.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, ThrowBlocksAroundCraters) {
    // --8<-- [start:throw_blocks_around_craters]
    const auto craters = CraterLayer::Manual({ManualCrater(0.0, 0.0, 60.0)});
    BoulderLayerOptions options;
    options.seed = 4;
    options.sizes.minDiameter = 0.5;
    options.ejecta.minCraterDiameter = 10.0;  // meters; smaller craters throw no blocks
    options.ejecta.distribution = nullptr;
    options.ejecta.coverage = 0.15;  // Golombek and Rapp blocks covering 15% of the rim
    options.ejecta.reach = 3.0;      // radii from the center
    const BoulderLayer boulders(options, craters);
    const BoulderList list = boulders.Boulders({-100.0, -100.0, 100.0, 100.0});
    // --8<-- [end:throw_blocks_around_craters]
    EXPECT_GT(list.Size(), 0u);
}

}  // namespace
}  // namespace terraingen
