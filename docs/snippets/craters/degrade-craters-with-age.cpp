// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Degrade Craters With Age by Freshness or by Diffusion
///
/// A crater layer's degradation model wears each crater by its age. Freshness degradation scales
/// a crater's depth and rim down linearly over its lifetime, while diffusion degradation wears it
/// as topographic diffusion would, quickly at first and then slowly, rounding the rim before the
/// bowl. The depth ratio each one gives a crater can be read without evaluating any heights.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, DegradeCratersWithAge) {
    Crater crater = ManualCrater(0.0, 0.0, 300.0);
    crater.ageGyr = 0.5;
    // --8<-- [start:degrade_craters_with_age]
    const auto ages = std::make_shared<FreshnessAges>(ValueDistribution::Constant(1.0), 3.0);
    CraterModels byFreshness;
    byFreshness.ages = ages;
    byFreshness.degradation =
        std::make_shared<FreshnessDegradation>(ages, LunarFreshCraterMorphometry);
    byFreshness.profile = std::make_shared<MartinParkesDunstanProfile>();
    PowerLawDiffusivity diffusivity;
    diffusivity.coefficient = 1.0;  // m^2/Myr
    diffusivity.exponent = 0.0;
    const auto profile =
        std::make_shared<DiffusedProfile>(std::make_shared<MartinParkesDunstanProfile>());
    CraterModels byDiffusion;
    byDiffusion.ages = std::make_shared<FreshAges>();
    byDiffusion.degradation =
        std::make_shared<DiffusionDegradation>(LunarFreshCraterMorphometry, diffusivity, profile);
    byDiffusion.profile = profile;
    const double freshnessRatio = byFreshness.Shape().Attributes(crater).depthRatio;
    const double diffusionRatio = byDiffusion.Shape().Attributes(crater).depthRatio;
    // --8<-- [end:degrade_craters_with_age]
    crater.ageGyr = 0.0;
    const double freshRatio = byFreshness.Shape().Attributes(crater).depthRatio;
    EXPECT_LT(freshnessRatio, freshRatio);
    EXPECT_LT(diffusionRatio, freshnessRatio);
}

}  // namespace
}  // namespace terraingen
