// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Write a Source of Your Own
///
/// A source gives a terrain's starting heights, as the grid it covers and the height at any point
/// and sample spacing. It can be an analytic surface, a DEM in a format of your own or another
/// simulation's ground. Layers are laid over it as over a GeoTIFF.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <cmath>
#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, WriteASource) {
    // --8<-- [start:write_a_source]
    // A Gaussian dome 20 m high and 60 m wide on a 129 x 129 grid of 1 m cells.
    class Dome final : public Source {
      public:
        RasterGrid Grid() const override {
            return RasterGrid::Centered({0.0, 0.0}, {129.0, 129.0}, 1.0);
        }

        float HeightAt(double x, double y, double /*spacing*/) const override {
            return static_cast<float>(20.0 * std::exp(-(x * x + y * y) / (2.0 * 30.0 * 30.0)));
        }

        void Fill(Raster<float>& heights, double spacing) const override {
            for (std::size_t row = 0; row < heights.Height(); ++row) {
                for (std::size_t col = 0; col < heights.Width(); ++col) {
                    const Vec2d center = heights.CellCenter(col, row);
                    heights(col, row) = HeightAt(center.x, center.y, spacing);
                }
            }
        }
    };

    TerrainGenerator terrain;
    terrain.source = std::make_shared<Dome>();
    terrain.layers = {std::make_shared<CraterLayer>()};
    const Raster<float> heights = Evaluate(terrain);
    // --8<-- [end:write_a_source]
    EXPECT_EQ(heights.Width(), 129);
}

}  // namespace
}  // namespace terraingen
