// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Evaluate a Synthetic Albedo Without Heights
///
/// A synthetic albedo is a base color in hue, saturation and value, optionally varied by fractal
/// noise in each channel, for terrain without albedo data. It needs no heights, so any grid can be
/// evaluated, here into three bands, red, green and blue, of a warm tint whose brightest band is
/// 0.11.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, EvaluateASyntheticAlbedo) {
    // --8<-- [start:evaluate_a_synthetic_albedo]
    SyntheticAlbedoOptions options;
    options.value = 0.11;
    options.rgb = true;
    options.hueDegrees = 30.0;
    options.saturation = 0.25;
    Albedo albedo;
    albedo.source = std::make_shared<SyntheticAlbedo>(options);
    const RasterGrid grid = RasterGrid::Centered({0.0, 0.0}, {100.0, 100.0}, 0.5);  // meters
    const std::vector<Raster<float>> bands = EvaluateAlbedo(albedo, grid, {});  // red, green, blue
    // --8<-- [end:evaluate_a_synthetic_albedo]
    ASSERT_EQ(bands.size(), 3u);
    EXPECT_EQ(bands[0].Width(), 200);
}

}  // namespace
}  // namespace terraingen
