// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Space Craters Apart
///
/// Generated craters land at random and may overlap freely. Spacing keeps craters of similar size
/// apart, dropping the lesser of two whose diameters differ by at most the ratio and whose
/// centers lie closer than the separation in larger diameters. It leaves an evenly spread field,
/// as on a surface where new craters have erased old ones.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <cmath>
#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, SpaceCratersApart) {
    CraterModels models;
    models.density = std::make_shared<PowerLawSfd>(3.0 * std::pow(10.0, -1.1), 2.0);
    models.ages = std::make_shared<FreshAges>();
    models.degradation = std::make_shared<NoDegradation>(LunarFreshCraterMorphometry);
    const Bounds2d region{-150.0, -150.0, 150.0, 150.0};
    // --8<-- [start:space_craters_apart]
    CraterLayerOptions options;
    options.seed = 4;
    options.generation.minDiameter = 5.0;
    options.generation.maxDiameter = 60.0;
    options.generation.spacing.diameterRatio = 2.0;
    options.generation.spacing.separation = 1.0;  // in larger diameters
    const CraterList spaced = CraterLayer(models, options).Craters(region);
    // --8<-- [end:space_craters_apart]
    const CraterSpacing spacing = options.generation.spacing;
    options.generation.spacing.separation = 0.0;
    const CraterList crowded = CraterLayer(models, options).Craters(region);
    EXPECT_LT(spaced.Size(), crowded.Size());
    for (std::size_t i = 0; i < spaced.Size(); ++i) {
        for (std::size_t j = i + 1; j < spaced.Size(); ++j) {
            EXPECT_FALSE(CratersConflict(spaced.At(i), spaced.At(j), spacing));
        }
    }
}

}  // namespace
}  // namespace terraingen
