// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Write a Size-Frequency Distribution of Your Own
///
/// A size-frequency distribution gives how many craters or boulders there are of each size, as the
/// cumulative count N(>= D) per square meter. Only `CumulativeAt` is needed, since the incremental
/// count and the inverse used to draw diameters are derived from it. Any crater layer or boulder
/// layer takes it in place of a published distribution.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <cmath>
#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, WriteASizeDistribution) {
    // --8<-- [start:write_a_size_distribution]
    // N(>= D) = 0.2 D^-2.5 craters per square meter, D in meters, from 1 m to 100 km.
    class SteepPowerLaw final : public SizeFrequencyDistribution {
      public:
        SteepPowerLaw() : SizeFrequencyDistribution(SfdKind::Density, {1.0, 1e5}) {}

      protected:
        double CumulativeAt(double diameter) const override {
            return 0.2 * std::pow(diameter, -2.5);
        }
    };

    const CraterModels models{std::make_shared<SteepPowerLaw>(), std::make_shared<FreshAges>(),
                              std::make_shared<NoDegradation>(LunarFreshCraterMorphometry),
                              std::make_shared<FassettThomsonProfile>()};
    CraterLayerOptions options;
    options.seed = 3;
    options.generation.dMin = 2.0;
    options.generation.dMax = 60.0;
    const CraterList craters = CraterLayer(models, options).Craters({-100.0, -100.0, 100.0, 100.0});
    // --8<-- [end:write_a_size_distribution]
    EXPECT_GT(craters.Size(), 0u);
}

}  // namespace
}  // namespace terraingen
