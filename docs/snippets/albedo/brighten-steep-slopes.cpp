// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Brighten Steep Slopes as Fresher Soil
///
/// Downslope creep keeps exposing immature soil on steep ground, so `SlopeAlbedo` raises the
/// optical maturity of slopes past an onset, 20 degrees by default, and the albedo with it. It
/// reads slopes on the terrain's heights, so the evaluation is given the ground beneath every
/// layer. Without a ground the albedo is left alone.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, BrightenSteepSlopes) {
    Fractal hills = Fractal::Fbm(2, 200.0, 70.0);
    hills.octaves = 6;
    const Terrain terrain(Surface{}, std::make_shared<FlatSource>(201, 201, 2.0),
                          {std::make_shared<NoiseLayer>(hills)});
    // --8<-- [start:brighten_steep_slopes]
    SlopeAlbedoOptions options;
    options.onsetDegrees = 20.0;
    Albedo albedo;
    albedo.source = std::make_shared<SyntheticAlbedo>(SyntheticAlbedoOptions{});
    albedo.layers = {std::make_shared<SlopeAlbedo>(options)};
    LayerContext context;
    context.ground = &terrain;  // after every layer
    const RasterGrid grid = terrain.Evaluate().Grid();
    const std::vector<Raster<float>> bands = EvaluateAlbedo(albedo, grid, context);
    // --8<-- [end:brighten_steep_slopes]
    ASSERT_EQ(bands.size(), 1u);
    const Span<const float> values = bands[0].Values();
    EXPECT_GT(*std::max_element(values.begin(), values.end()), 0.12f);
}

}  // namespace
}  // namespace terraingen
