// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief List the Craters of a Layer Over a Region
///
/// A crater layer on its own lists the craters it places over a region, as centers, diameters,
/// ages and IDs, with no terrain around it. The same seed and region always give the same craters.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <algorithm>

namespace terraingen {
namespace {

TEST_F(SnippetTest, ListCratersOfALayer) {
    // --8<-- [start:list_craters_of_a_layer]
    CraterLayerOptions options;
    options.seed = 7;
    options.generation.dMin = 4.0;
    options.generation.dMax = 120.0;
    const CraterLayer craters(DefaultCraterModels(), options);
    const CraterList list = craters.Craters({-200.0, -200.0, 200.0, 200.0});  // meters
    const double largest = *std::max_element(list.Diameters().begin(), list.Diameters().end());
    // --8<-- [end:list_craters_of_a_layer]
    EXPECT_GT(list.Size(), 0u);
    EXPECT_LE(largest, 120.0);
}

}  // namespace
}  // namespace terraingen
