// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Noise Relief Beneath the Craters for the Quickstart
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, QuickstartAddNoise) {
    TerrainGenerator terrain;
    terrain.source = std::make_shared<FlatSource>(257, 257, 0.5);
    // --8<-- [start:add_noise]
    Fractal fractal = Fractal::Fbm(1, 80.0, 2.0);  // seed, wavelength and amplitude in meters
    fractal.octaves = 5;
    const auto noise = std::make_shared<NoiseLayer>(fractal);
    const auto craters = std::make_shared<CraterLayer>();
    terrain.layers = {noise, craters};  // layers apply in order
    // --8<-- [end:add_noise]
    EXPECT_EQ(Evaluate(terrain).Width(), 257);
}

}  // namespace
}  // namespace terraingen
