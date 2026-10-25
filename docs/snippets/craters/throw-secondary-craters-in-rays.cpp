// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Throw Secondary Craters in Rays
///
/// A secondary crater layer scatters the craters a primary layer's craters throw, from their
/// onset range out to about 1.6 times the median ray length, sized after Singer et al. With rays
/// on, a share of them gather into ray chains and clusters. Listing them needs no heights; add
/// both layers to a terrain to draw them.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <cmath>

namespace terraingen {
namespace {

TEST_F(SnippetTest, ThrowSecondaryCratersInRays) {
    // --8<-- [start:throw_secondary_craters_in_rays]
    const auto primary = CraterLayer::Manual({ManualCrater(0.0, 0.0, 2000.0)});
    SecondaryCraterOptions options;
    options.seed = 3;
    options.onsetCount = 1000.0;
    options.minDiameter = 24.0;  // meters
    options.rays.enabled = true;
    options.rays.share = 0.7;  // 70% of them gather in rays
    const SecondaryCraterLayer secondaries(primary, options);
    const CraterList list = secondaries.Secondaries({-11000.0, -11000.0, 11000.0, 11000.0});
    // --8<-- [end:throw_secondary_craters_in_rays]
    EXPECT_GT(list.Size(), 100u);
    EXPECT_GE(*std::min_element(list.Diameters().begin(), list.Diameters().end()), 24.0);
    for (std::size_t i = 0; i < list.Size(); ++i) {
        EXPECT_GT(std::hypot(list.X()[i], list.Y()[i]), 1000.0);
    }
}

}  // namespace
}  // namespace terraingen
