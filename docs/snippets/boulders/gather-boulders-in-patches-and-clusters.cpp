// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Gather Boulders in Patches and Clusters
///
/// Patches vary the abundance of boulders across the ground in fractal patches of a typical size,
/// from a fraction to a multiple of the mean. Clusters crowd a share of the boulders around centers
/// about a spacing apart, keeping the mean abundance. Thomas clusters scatter their centers at
/// random and let overlapping clusters add.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, GatherBouldersInPatchesAndClusters) {
    const auto BoulderOptions = [] {
        BoulderLayerOptions options;  // 0.3 m and up from NASA SP-8023's maximum intercrater curve
        options.seed = 4;
        options.sizes.distribution =
            std::make_shared<TabulatedSfd>(Sp8023IntercraterBlocks(SpecificationCurve::Maximum));
        options.sizes.minDiameter = 0.3;
        return options;
    };
    const Bounds2d region{-40.0, -40.0, 40.0, 40.0};

    // --8<-- [start:gather_boulders_in_patches_and_clusters]
    BoulderLayerOptions patchy = BoulderOptions();
    patchy.patches.spread = 5.0;  // a fifth to 5 times the mean abundance
    patchy.patches.size = 30.0;   // meters
    BoulderLayerOptions clustered = BoulderOptions();
    clustered.clusters.share = 0.9;
    clustered.clusters.centerSpacing = 20.0;
    clustered.clusters.radius = 3.0;
    clustered.clusters.model = BoulderClusterModel::Thomas;
    const BoulderList patches = BoulderLayer(patchy).Boulders(region);
    const BoulderList clusters = BoulderLayer(clustered).Boulders(region);
    // --8<-- [end:gather_boulders_in_patches_and_clusters]
    EXPECT_GT(patches.Size(), 0u);
    EXPECT_GT(clusters.Size(), 0u);
}

}  // namespace
}  // namespace terraingen
