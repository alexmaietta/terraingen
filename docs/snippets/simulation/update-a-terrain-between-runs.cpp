// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Update a Terrain Between Simulation Runs
///
/// A terrain's parts are immutable and shared, so a simulation can change one between runs and
/// keep the rest. `SetLayer` replaces a layer, and a `StageCache` keeps the heights beneath it, so
/// evaluating the same grid again applies only the layers from there on. The cache holds one grid
/// of heights and is refreshed whenever anything beneath its stage changes.
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
    StageCache beneathCraters(1);  // the heights before layer 1, the craters
    const EvaluationOptions options = EvaluationOptions{}.Keep(beneathCraters);
    const Raster<float> first = terrain.Evaluate(source->Grid(), options);

    CraterLayerOptions craterOptions;
    craterOptions.seed = 2;
    terrain.SetLayer(1, std::make_shared<CraterLayer>(DefaultCraterModels(), craterOptions));
    const Raster<float> second = terrain.Evaluate(source->Grid(), options);  // hills not redone
    // --8<-- [end:update_a_terrain_between_runs]
    EXPECT_FALSE(beneathCraters.Empty());
    EXPECT_FALSE(std::equal(first.Values().begin(), first.Values().end(), second.Values().begin()));
}

}  // namespace
}  // namespace terraingen
