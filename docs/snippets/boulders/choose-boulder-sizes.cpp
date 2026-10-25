// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Choose How Boulders Are Sized
///
/// By default boulders between craters follow NASA SP-8023's nominal intercrater curve. Without a
/// distribution they follow Golombek and Rapp's model for a share of the ground covered by rock,
/// and any size-frequency distribution, such as a power law, can stand in for either. The minimum
/// and maximum diameters bound every choice.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>
#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, ChooseBoulderSizes) {
    const Bounds2d region{-25.0, -25.0, 25.0, 25.0};

    // --8<-- [start:choose_boulder_sizes]
    BoulderSizes nominal;  // NASA SP-8023's nominal intercrater curve
    BoulderSizes rocky;
    rocky.distribution = nullptr;
    rocky.coverage = 0.1;  // Golombek and Rapp, 10% of the ground covered
    BoulderSizes powerLaw;
    powerLaw.distribution = std::make_shared<PowerLawSfd>(0.005, 2.5);
    std::vector<BoulderList> lists;
    for (BoulderSizes sizes : {nominal, rocky, powerLaw}) {
        sizes.minDiameter = 0.3;  // meters
        sizes.maxDiameter = 4.0;
        BoulderLayerOptions options;
        options.sizes = sizes;
        options.seed = 4;
        lists.push_back(BoulderLayer(options).Boulders(region));
    }
    // --8<-- [end:choose_boulder_sizes]
    for (const BoulderList& list : lists) {
        EXPECT_GT(list.Size(), 0u);
    }
    EXPECT_GT(lists[1].Size(), lists[0].Size());
}

}  // namespace
}  // namespace terraingen
