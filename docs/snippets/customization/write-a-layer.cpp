// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Write a Layer of Your Own
///
/// A layer changes the heights beneath it. One whose height at a point depends only on the height
/// there and the point derives from `FormulaLayer` (in Python, `Layer` with `apply_at`), and is
/// evaluated over grids and at points, on every thread, like the built-in layers. Layers after it,
/// here craters, are laid over what it makes.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <cmath>
#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, WriteALayer) {
    // --8<-- [start:write_a_layer]
    // Ridges 2 m high and 40 m apart, running north.
    class Ridges final : public FormulaLayer {
      protected:
        float HeightAt(float height, double x, double /*y*/,
                       const LayerContext& /*context*/) const override {
            return height + static_cast<float>(2.0 * std::cos(2.0 * kPi * x / 40.0));
        }
    };

    const Terrain terrain(Surface{}, std::make_shared<FlatSource>(129, 129, 1.0),
                          {std::make_shared<Ridges>(), std::make_shared<CraterLayer>()});
    const Raster<float> heights = terrain.Evaluate();
    // --8<-- [end:write_a_layer]
    EXPECT_EQ(heights.Width(), 129);
}

}  // namespace
}  // namespace terraingen
