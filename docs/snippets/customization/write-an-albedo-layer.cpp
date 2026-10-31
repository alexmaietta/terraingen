// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Write an Albedo Layer of Your Own
///
/// An albedo layer changes the albedo beneath it, one point at a time, from the value there and
/// the point. It is laid over any albedo source with the built-in layers, in order. Here two
/// rover wheel tracks along a winding path darken a synthetic albedo.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <cmath>
#include <memory>
#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, WriteAnAlbedoLayer) {
    // --8<-- [start:write_an_albedo_layer]
    // Two wheel tracks 0.4 m wide and 1.8 m apart along a winding path, 20 percent darker.
    class RoverTracks final : public AlbedoLayer {
      public:
        void ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                     const LayerContext& /*context*/) const override {
            const double middle = 3.0 * std::sin(x / 8.0);
            if (std::abs(y - middle + 0.9) < 0.2 || std::abs(y - middle - 0.9) < 0.2) {
                for (std::size_t band = 0; band < bands; ++band) {
                    value[band] *= 0.8f;
                }
            }
        }
    };

    SyntheticAlbedoOptions options;
    options.value = 0.11;
    Fractal soil = Fractal::Fbm(1, 10.0, 0.2);
    soil.octaves = 5;
    options.valueNoise = {soil};
    Albedo albedo;
    albedo.source = std::make_shared<SyntheticAlbedo>(options);
    albedo.layers = {std::make_shared<RoverTracks>()};
    const RasterGrid grid = RasterGrid::Centered({0.0, 0.0}, {40.0, 16.0}, 0.05);  // meters
    const std::vector<Raster<float>> bands = EvaluateAlbedo(albedo, grid, {});
    // --8<-- [end:write_an_albedo_layer]
    ASSERT_EQ(bands.size(), 1u);
    EXPECT_EQ(bands[0].Width(), 800u);
}

}  // namespace
}  // namespace terraingen
