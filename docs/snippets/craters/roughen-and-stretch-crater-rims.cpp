// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Roughen and Stretch Crater Rims
///
/// Irregularity in the apply options roughens crater rims with random harmonics and stretches a
/// share of craters into ellipses, as low-angle impacts make them. Each crater draws its outline
/// from its ID, so the outline a crater gets can be read without evaluating any heights. Manual
/// craters stay round unless the selection includes them.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, RoughenAndStretchCraterRims) {
    CraterList placed;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            Crater crater = ManualCrater((column - 1) * 240.0, (1 - row) * 240.0, 150.0);
            crater.ageGyr = 0.01;
            placed.Append(crater);
        }
    }
    // --8<-- [start:roughen_and_stretch_crater_rims]
    CraterIrregularity irregularity;
    irregularity.enabled = true;
    irregularity.seed = 3;
    irregularity.selection.manual = true;  // manual craters stay round otherwise
    irregularity.roughness = ValueDistribution::Constant(0.06);  // RMS deviation, in radii
    irregularity.elongatedShare = 0.5;
    irregularity.ellipticity = ValueDistribution::Uniform(1.2, 1.6);  // long over short axis
    CraterApplyOptions apply;
    apply.irregularity = irregularity;
    const auto craters = CraterLayer::Manual(placed, DefaultCraterModels(), apply);
    // --8<-- [end:roughen_and_stretch_crater_rims]
    const CraterList list = craters->Craters({-400.0, -400.0, 400.0, 400.0});
    ASSERT_EQ(list.Size(), 9u);
    int elongated = 0;
    for (std::size_t i = 0; i < list.Size(); ++i) {
        const CraterOutline outline = IrregularOutline(list.At(i), irregularity);
        EXPECT_FALSE(outline.Round());
        elongated += outline.ellipticity > 1.2 ? 1 : 0;
    }
    EXPECT_GT(elongated, 0);
}

}  // namespace
}  // namespace terraingen
