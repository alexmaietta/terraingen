// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Place Craters by Hand
///
/// A manual crater layer holds only the craters you list, such as a catalog of a landing site,
/// and generates none. Each crater takes its depth and rim from the layer's models unless you
/// give its rim-to-floor depth in meters. Listing the layer's craters gives each one's floor
/// depth, rim height and depth ratio as applied, without evaluating any heights.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <cmath>

namespace terraingen {
namespace {

TEST_F(SnippetTest, PlaceCratersByHand) {
    // --8<-- [start:place_craters_by_hand]
    Crater deep = ManualCrater(0.0, 0.0, 20.0);
    deep.depth = 3.0;  // rim to floor, in meters
    Crater shallow = ManualCrater(30.0, 0.0, 20.0);
    shallow.depth = 1.2;
    const auto craters = CraterLayer::Manual({ManualCrater(-30.0, 0.0, 20.0), deep, shallow});
    const CraterList list = craters->Craters({-50.0, -20.0, 50.0, 20.0});
    const CraterAttributes applied = craters->Models().Shape().Attributes(list.At(1));
    // --8<-- [end:place_craters_by_hand]
    ASSERT_EQ(list.Size(), 3u);
    EXPECT_NEAR(applied.depthRatio, 3.0 / 20.0, 1e-9);
    EXPECT_NEAR(applied.floorDepth + applied.rimHeight, 3.0, 1e-4);
}

}  // namespace
}  // namespace terraingen
