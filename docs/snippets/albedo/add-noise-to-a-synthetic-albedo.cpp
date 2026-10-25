// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Add Noise to a Synthetic Albedo
///
/// Each channel of a synthetic albedo can be varied by a stack of fractals. Value noise is
/// relative, so an amplitude of 0.5 makes the value swing up to half of itself either way, while
/// hue noise is in degrees and saturation noise in saturation. Broad noise in hue and saturation
/// with finer noise in value gives a tinted ground with patches of darker and brighter soil.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, AddNoiseToASyntheticAlbedo) {
    // --8<-- [start:add_noise_to_a_synthetic_albedo]
    SyntheticAlbedoOptions options;
    options.value = 0.11;
    options.valueNoise = {Fractal::Fbm(1, 120.0, 0.5)};
    options.rgb = true;
    options.hueDegrees = 30.0;
    options.saturation = 0.25;
    options.hueNoiseDegrees = {Fractal::Fbm(2, 200.0, 15.0)};
    options.saturationNoise = {Fractal::Fbm(3, 200.0, 0.1)};
    Albedo albedo;
    albedo.source = std::make_shared<SyntheticAlbedo>(options);
    const RasterGrid grid = RasterGrid::Centered({0.0, 0.0}, {400.0, 400.0}, 2.0);  // meters
    const std::vector<Raster<float>> bands = EvaluateAlbedo(albedo, grid, {});  // red, green, blue
    // --8<-- [end:add_noise_to_a_synthetic_albedo]
    ASSERT_EQ(bands.size(), 3u);
    const Span<const float> red = bands[0].Values();
    const auto range = std::minmax_element(red.begin(), red.end());
    EXPECT_LT(*range.first, 0.1f);
    EXPECT_GT(*range.second, 0.12f);
}

}  // namespace
}  // namespace terraingen
