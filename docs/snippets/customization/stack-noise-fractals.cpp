// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Stack Noise Fractals Into a Layer
///
/// A noise layer adds a stack of fractals, each a sum of octaves of a basis noise. Here broad fBm
/// hills come first, then finer ridged noise on top. Each fractal sets its own seed, wavelength,
/// amplitude, octaves and type, and how it combines with those before it.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, StackNoiseFractals) {
    // --8<-- [start:stack_noise_fractals]
    Fractal hills = Fractal::Fbm(1, 200.0, 6.0);  // seed, wavelength and amplitude in meters
    hills.octaves = 4;
    Fractal ridges = Fractal::Fbm(2, 60.0, 1.5);
    ridges.type = FractalType::Ridged;
    ridges.octaves = 5;

    const auto noise = std::make_shared<NoiseLayer>(std::vector<Fractal>{hills, ridges});
    const Terrain terrain(Surface{}, std::make_shared<FlatSource>(257, 257, 1.0), {noise});
    const Raster<float> heights = terrain.Evaluate();
    // --8<-- [end:stack_noise_fractals]
    EXPECT_EQ(heights.Width(), 257);
}

}  // namespace
}  // namespace terraingen
