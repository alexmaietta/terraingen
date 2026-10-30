// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Terrace the Walls of Large Craters
///
/// Terraces in the apply options slump the walls of large complex craters into benches and
/// scarps. They begin at the onset diameter with one bench over part of the wall and grow to the
/// most benches around most of it, each crater drawing its own from its ID.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <cmath>
#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, TerraceCraterWalls) {
    CraterModels models;
    models.ages = std::make_shared<FreshAges>();
    models.degradation = std::make_shared<NoDegradation>(LunarFreshCraterMorphometry);
    models.profile = std::make_shared<ComplexCraterProfile>(
        std::make_shared<MartinParkesDunstanProfile>(), LunarComplexCraterShape());
    // --8<-- [start:terrace_crater_walls]
    CraterApplyOptions apply;
    apply.terraces.enabled = true;
    apply.terraces.onsetDiameter = 20000.0;  // meters
    apply.terraces.maxBenches = 4;
    const auto craters = CraterLayer::Manual({ManualCrater(0.0, 0.0, 60000.0)}, models, apply);
    Terrain terrain(Surface{}, std::make_shared<FlatSource>(221, 221, 400.0), {craters});
    const Raster<float> heights = terrain.Evaluate();
    // --8<-- [end:terrace_crater_walls]
    terrain.SetLayer(0, CraterLayer::Manual({ManualCrater(0.0, 0.0, 60000.0)}, models, {}));
    const Raster<float> plain = terrain.Evaluate();
    float largest = 0.0f;
    for (std::size_t i = 0; i < heights.Size(); ++i) {
        largest = std::fmax(largest, std::fabs(heights.Values()[i] - plain.Values()[i]));
    }
    EXPECT_GT(largest, 100.0f);
}

}  // namespace
}  // namespace terraingen
