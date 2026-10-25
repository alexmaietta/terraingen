// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Write a Crater Age Model of Your Own
///
/// A crater age model gives how long craters of each size stay visible and draws each crater's
/// age within that lifetime. Freshness degradation wears craters over the same lifetimes, so the
/// ages also set how worn each crater is. Here small craters are erased quickly and large ones
/// last, so small craters are always young.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, WriteACraterAgeModel) {
    // --8<-- [start:write_a_crater_age_model]
    // Craters last 10 Myr per meter of diameter, up to 3.5 Gyr, so small ones are always young.
    class ErasedBySize final : public CraterAgeModel {
      protected:
        double LifetimeAt(double diameter) const override {
            return std::min(0.01 * diameter, 3.5);
        }

        double SampleAgeAt(RandomStream& random, double /*diameter*/,
                           double lifetimeGyr) const override {
            return lifetimeGyr * random.NextDouble();
        }
    };

    const auto ages = std::make_shared<ErasedBySize>();
    const CraterModels models{
        std::make_shared<PowerLawSfd>(0.05, 2.0), ages,
        std::make_shared<FreshnessDegradation>(ages, LunarFreshCraterMorphometry),
        std::make_shared<MartinParkesDunstanProfile>()};
    CraterLayerOptions options;
    options.seed = 4;
    options.generation.dMin = 4.0;
    options.generation.dMax = 200.0;
    const CraterList craters = CraterLayer(models, options).Craters({-250.0, -250.0, 250.0, 250.0});
    // --8<-- [end:write_a_crater_age_model]
    ASSERT_GT(craters.Size(), 0u);
    for (std::size_t i = 0; i < craters.Size(); ++i) {
        EXPECT_LE(craters.AgesGyr()[i], 0.01 * craters.Diameters()[i] + 1e-9);
    }
}

}  // namespace
}  // namespace terraingen
