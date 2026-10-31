// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Write a Surface Field of Your Own
///
/// A surface field is a value that varies over the ground, read at any point. Crater and boulder
/// layers take one as a density map, which scales their density point by point or gives a count
/// or covered share to match. It also needs its largest value, which bounds how many features are
/// drawn before thinning. Here a field clears craters from a landing pad and lets them back in
/// around it.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, WriteASurfaceField) {
    // --8<-- [start:write_a_surface_field]
    // No craters within 60 m of the center, rising to the layer's own density at 100 m.
    class LandingPad final : public SurfaceField {
      public:
        double ValueAt(double x, double y) const override {
            return std::clamp((std::hypot(x, y) - 60.0) / 40.0, 0.0, 1.0);
        }

        double Max() const override {
            return 1.0;
        }
    };

    DensityMap densityMap;
    densityMap.field = std::make_shared<LandingPad>();
    densityMap.measure = DensityMeasure::Scale;  // times the layer's own density
    CraterLayerOptions options;
    options.seed = 5;
    options.generation.minDiameter = 4.0;
    options.generation.maxDiameter = 40.0;
    options.densityMap = densityMap;
    const CraterLayer craters(DefaultCraterModels(), options);
    const CraterList list = craters.Craters({-200.0, -200.0, 200.0, 200.0});
    // --8<-- [end:write_a_surface_field]
    ASSERT_GT(list.Size(), 0u);
    for (std::size_t i = 0; i < list.Size(); ++i) {
        EXPECT_GE(std::hypot(list.X()[i], list.Y()[i]), 60.0);
    }
}

}  // namespace
}  // namespace terraingen
