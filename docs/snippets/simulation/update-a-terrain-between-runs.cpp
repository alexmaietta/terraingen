// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Update a Terrain Between Simulation Runs
///
/// A terrain's parts are immutable and shared, so a simulation can change one between runs and
/// keep the rest. `SetLayer` replaces a layer, and a `TerrainView` over a grid keeps the heights
/// beneath its last layer, so updating it after replacing that layer applies only that layer. An
/// update after a change beneath it evaluates the whole terrain again.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, UpdateATerrainBetweenRuns) {
    // --8<-- [start:update_a_terrain_between_runs]
    const auto source = std::make_shared<FlatSource>(129, 129, 1.0);
    const auto hills = std::make_shared<NoiseLayer>(Fractal::Fbm(1, 120.0, 4.0));
    Terrain terrain(Surface{}, source, {hills, std::make_shared<CraterLayer>()});
    TerrainView view(source->Grid());  // keeps the heights before the last layer, the craters
    const Raster<float> first = view.Update(terrain);

    CraterLayerOptions craterOptions;
    craterOptions.seed = 2;
    terrain.SetLayer(1, std::make_shared<CraterLayer>(DefaultCraterModels(), craterOptions));
    const Raster<float>& second = view.Update(terrain);  // the hills are not evaluated again
    // --8<-- [end:update_a_terrain_between_runs]
    EXPECT_FALSE(std::equal(first.Values().begin(), first.Values().end(), second.Values().begin()));
}

}  // namespace
}  // namespace terraingen
