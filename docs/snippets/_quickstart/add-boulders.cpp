// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Boulders Between and Around the Craters for the Quickstart
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, QuickstartAddBoulders) {
    TerrainGenerator terrain;
    terrain.source = std::make_shared<FlatSource>(257, 257, 0.5);
    Fractal fractal = Fractal::Fbm(1, 80.0, 2.0);
    fractal.octaves = 5;
    const auto noise = std::make_shared<NoiseLayer>(fractal);
    const auto craters = std::make_shared<CraterLayer>();
    // --8<-- [start:add_boulders]
    // With ejecta thrown out by those craters.
    const auto boulders = std::make_shared<BoulderLayer>(BoulderLayerOptions{}, craters);
    terrain.layers = {noise, craters, boulders};
    // --8<-- [end:add_boulders]
    EXPECT_EQ(Evaluate(terrain).Width(), 257);
}

}  // namespace
}  // namespace terraingen
