// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Write a Degradation Model of Your Own
///
/// A degradation model gives each crater's state, its depth and rim height when fresh and now, from
/// the crater alone, its diameter, age and ID. The profile shapes the crater to it, and crater
/// lists report it as each crater's depth ratio and freshness.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <cmath>
#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, WriteADegradationModel) {
    // --8<-- [start:write_a_degradation_model]
    // Craters 0.2 times as deep as wide when fresh, losing half that depth every Gyr.
    class ShallowWithAge final : public DegradationModel {
      public:
        CraterState State(const Crater& crater) const override {
            const CraterMorphometry fresh{0.2, 0.04};
            const double share = std::pow(0.5, crater.ageGyr);
            return {fresh, {0.2 * share, 0.04 * share}, 0.0};
        }
    };

    const EquilibriumSurface surface{
        std::make_shared<SplicedSfd>(ExtendedNeukumProductionFunction()),
        std::make_shared<PowerLawSfd>(TraskEquilibrium()), 3.5, NeukumLunarChronology};
    const CraterModels models{surface.Density(), surface.Ages(), std::make_shared<ShallowWithAge>(),
                              std::make_shared<MartinParkesDunstanProfile>()};
    const CraterLayer craters(models);
    const CraterFeatures listed = DescribeCraters(craters.Craters({-100.0, -100.0, 100.0, 100.0}),
                                                  models.Shape());  // with each state
    // --8<-- [end:write_a_degradation_model]
    ASSERT_GT(listed.attributes.size(), 0u);
}

}  // namespace
}  // namespace terraingen
