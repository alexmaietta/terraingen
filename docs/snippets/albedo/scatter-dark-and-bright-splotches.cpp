// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Scatter Dark and Bright Splotches
///
/// Splotches are the small dark and bright patches that low-velocity secondary impacts leave in
/// loose regolith, forming and fading over tens of thousands of years. `SplotchAlbedo` scatters
/// them by the measured formation rate, nine in ten dark by default. Here half are bright, and
/// only those at least 3 m across are drawn, to keep the ground between them clear.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, ScatterDarkAndBrightSplotches) {
    // --8<-- [start:scatter_dark_and_bright_splotches]
    SplotchAlbedoOptions options;
    options.seed = 3;
    options.darkShare = 0.5;
    options.brightShare = 0.5;
    options.minDiameter = 3.0;  // meters
    Albedo albedo;
    albedo.source = std::make_shared<SyntheticAlbedo>(SyntheticAlbedoOptions{});
    albedo.layers = {std::make_shared<SplotchAlbedo>(options)};
    const RasterGrid grid = RasterGrid::Centered({0.0, 0.0}, {100.0, 100.0}, 0.25);  // meters
    const std::vector<Raster<float>> bands = EvaluateAlbedo(albedo, grid, {});
    // --8<-- [end:scatter_dark_and_bright_splotches]
    ASSERT_EQ(bands.size(), 1u);
    const Span<const float> values = bands[0].Values();
    const auto range = std::minmax_element(values.begin(), values.end());
    EXPECT_LT(*range.first, 0.11f);
    EXPECT_GT(*range.second, 0.11f);
}

}  // namespace
}  // namespace terraingen
