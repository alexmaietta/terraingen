// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief List the Boulders of a Layer Over a Region
///
/// A boulder layer on its own lists the boulders it places over a region, with no ground beneath
/// them, so their ground heights are unknown. By default their sizes follow NASA SP-8023's nominal
/// intercrater curve.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <algorithm>

namespace terraingen {
namespace {

TEST_F(SnippetTest, ListBouldersOfALayer) {
    // --8<-- [start:list_boulders_of_a_layer]
    BoulderLayerOptions options;
    options.seed = 4;
    options.sizes.minDiameter = 0.3;
    options.sizes.maxDiameter = 5.0;
    const BoulderLayer boulders(options);
    const BoulderList list = boulders.Boulders({-30.0, -30.0, 30.0, 30.0});  // meters
    // --8<-- [end:list_boulders_of_a_layer]
    ASSERT_GT(list.Size(), 0u);
    EXPECT_LE(*std::max_element(list.Diameters().begin(), list.Diameters().end()), 5.0);
}

}  // namespace
}  // namespace terraingen
