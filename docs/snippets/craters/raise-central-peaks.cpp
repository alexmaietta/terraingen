// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Raise Central Peaks in Complex Craters
///
/// A complex crater profile gives craters above the simple to complex transition, 15 km on the
/// Moon, a flat floor and a round central dome. Peaks in the apply options raise that dome into a
/// cluster of massifs drawn per crater from its ID, sized by the profile's peak height and base.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, RaiseCentralPeaks) {
    // --8<-- [start:raise_central_peaks]
    CraterModels models;
    models.ages = std::make_shared<FreshAges>();
    models.degradation = std::make_shared<NoDegradation>(LunarFreshCraterMorphometry);
    models.profile = std::make_shared<ComplexCraterProfile>(
        std::make_shared<MartinParkesDunstanProfile>(), LunarComplexCraterShape());
    CraterApplyOptions apply;
    apply.peaks.enabled = true;
    apply.peaks.minMassifs = 2;
    apply.peaks.maxMassifs = 5;
    const auto craters = CraterLayer::Manual({ManualCrater(0.0, 0.0, 40000.0)}, models, apply);
    const Terrain terrain(Surface{}, std::make_shared<FlatSource>(201, 201, 300.0), {craters});
    const Raster<float> heights = terrain.Evaluate();
    // --8<-- [end:raise_central_peaks]
    const CraterList list = craters->Craters({-1.0, -1.0, 1.0, 1.0});
    const CraterAttributes applied = models.Shape().Attributes(list.At(0));
    EXPECT_TRUE(applied.complex);
    float peak = heights(100, 100);
    for (std::size_t row = 80; row <= 120; ++row) {
        for (std::size_t column = 80; column <= 120; ++column) {
            peak = std::max(peak, heights(column, row));
        }
    }
    EXPECT_GT(peak, -applied.floorDepth + 300.0);
}

}  // namespace
}  // namespace terraingen
