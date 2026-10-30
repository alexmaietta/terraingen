// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Write a Source of Your Own
///
/// A source gives a terrain's starting heights, as the grid it covers and, in one method that
/// serves grids and points alike, the heights of a block of samples at a sample spacing. It can be
/// an analytic surface, a DEM in a format of your own or another simulation's ground. Layers are
/// laid over it as over a GeoTIFF.
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

      protected:
        void FillSamples(SampleBlock& samples, double /*spacing*/,
                         unsigned /*threads*/) const override {
            for (std::size_t i = 0; i < samples.Size(); ++i) {
                const Vec2d p = samples.Position(i);
                const double r2 = p.x * p.x + p.y * p.y;
                samples.Height(i) = static_cast<float>(20.0 * std::exp(-r2 / (2.0 * 30.0 * 30.0)));
            }
        }
    };

    const Terrain terrain(Surface{}, std::make_shared<Dome>(), {std::make_shared<CraterLayer>()});
    const Raster<float> heights = terrain.Evaluate();
    // --8<-- [end:write_a_source]
    EXPECT_EQ(heights.Width(), 129);
}

}  // namespace
}  // namespace terraingen
