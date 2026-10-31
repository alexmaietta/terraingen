// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Read the Ground Beneath a Layer
///
/// `Beneath(k)` is the terrain made of its source and its first `k` layers, sharing their parts and
/// caches with the whole terrain. It evaluates and answers point queries like any terrain, so the
/// relief one layer adds, such as what craters cut into the hills, is a difference of two grids.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>
#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, ReadTheGroundBeneathALayer) {
    // --8<-- [start:read_the_ground_beneath_a_layer]
    const Terrain terrain(Surface{}, std::make_shared<FlatSource>(129, 129, 1.0),
                          {std::make_shared<NoiseLayer>(Fractal::Fbm(1, 120.0, 4.0)),
                           std::make_shared<CraterLayer>()});
    const Raster<float> hills = terrain.Beneath(1).Evaluate();  // the source and the noise
    const Raster<float> cratered = terrain.Evaluate();
    std::vector<float> craterRelief(cratered.Values().size());
    for (std::size_t i = 0; i < craterRelief.size(); ++i) {
        craterRelief[i] = cratered.Values()[i] - hills.Values()[i];
    }
    // --8<-- [end:read_the_ground_beneath_a_layer]
    EXPECT_EQ(terrain.Beneath(1).Layers().size(), 1u);
    bool changed = false;
    for (const float relief : craterRelief) {
        changed = changed || relief != 0.0f;
    }
    EXPECT_TRUE(changed);
}

}  // namespace
}  // namespace terraingen
