// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Write a Crater Profile of Your Own
///
/// A crater profile gives a crater's height by distance from its center, shaped by its current
/// depth and rim height as fractions of its diameter, which the degradation model sets. It
/// also says how many radii from the center it reaches, so the layer knows which craters touch a
/// point.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <cmath>
#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, WriteACraterProfile) {
    // --8<-- [start:write_a_crater_profile]
    // A straight-walled cone to the rim, and a rim that falls off as the cube of distance.
    class ConeProfile final : public CraterProfile {
      public:
        double Height(double distance, double diameter, const CraterState& state) const override {
            const double x = distance / (0.5 * diameter);
            const double depth = state.current.depthRatio * diameter;
            const double rim = state.current.rimHeightRatio * diameter;
            return x <= 1.0 ? rim - depth * (1.0 - x) : rim / (x * x * x);
        }

        double ReachRadii() const override {
            return 2.0;
        }
    };

    const CraterModels models{std::make_shared<PowerLawSfd>(0.05, 2.0),
                              std::make_shared<FreshAges>(),
                              std::make_shared<NoDegradation>(LunarFreshCraterMorphometry),
                              std::make_shared<ConeProfile>()};
    CraterLayerOptions options;
    options.seed = 1;
    options.generation.minDiameter = 6.0;
    options.generation.maxDiameter = 60.0;
    const Terrain terrain(Surface{}, std::make_shared<FlatSource>(129, 129, 1.0),
                          {std::make_shared<CraterLayer>(models, options)});
    const Raster<float> heights = terrain.Evaluate();
    // --8<-- [end:write_a_crater_profile]
    EXPECT_EQ(heights.Width(), 129);
}

}  // namespace
}  // namespace terraingen
