// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Use Published Crater Models
///
/// A crater layer is made of its models, which set how many craters there are of each size, how old
/// they are, how they degrade and the shape of a fresh one. Here they describe a 3.5 Gyr lunar mare
/// with the extended Neukum production function capped by Trask's equilibrium, steady-state ages on
/// Neukum's chronology, degradation by freshness, and Fassett and Thomson's profile.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, UsePublishedCraterModels) {
    // --8<-- [start:use_published_crater_models]
    const EquilibriumSurface surface{
        std::make_shared<SplicedSfd>(ExtendedNeukumProductionFunction()),
        std::make_shared<PowerLawSfd>(TraskEquilibrium()), 3.5, NeukumLunarChronology};
    const auto ages = surface.Ages();
    const CraterModels models{
        surface.Density(), ages,
        std::make_shared<FreshnessDegradation>(ages, LunarFreshCraterMorphometry),
        std::make_shared<FassettThomsonProfile>()};
    CraterLayerOptions options;
    options.seed = 7;
    options.generation.minDiameter = 4.0;
    options.generation.maxDiameter = 120.0;
    const auto craters = std::make_shared<CraterLayer>(models, options);
    // --8<-- [end:use_published_crater_models]
    EXPECT_GT(craters->Craters({-100.0, -100.0, 100.0, 100.0}).Size(), 0u);
}

}  // namespace
}  // namespace terraingen
