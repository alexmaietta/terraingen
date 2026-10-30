// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Draw Boulders as Relief or as 3D Models
///
/// Boulders are drawn into the heights as domes, which are cheap, or as the tops of their 3D
/// shapes. Those at or above a model threshold become 3D models of their own instead and are left
/// out of the heights, sharing a few prototype shapes so they can be instanced.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>
#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, DrawBouldersAsReliefOrModels) {
    BoulderLayerOptions options;
    options.seed = 1;
    options.sizes.distribution = nullptr;
    options.sizes.coverage = 0.2;
    options.sizes.minDiameter = 0.3;

    // --8<-- [start:draw_boulders_as_relief_or_models]
    BoulderRendering& rendering = options.rendering;
    rendering.relief = BoulderRelief::Shape;     // Dome is cheap; Shape draws each 3D shape's top
    rendering.modelThreshold.minDiameter = 1.0;  // meters
    rendering.shapePrototypes = 16;              // boulders share 16 shapes; 0 gives each its own
    const auto source = std::make_shared<FlatSource>(201, 201, 0.05);
    const Terrain terrain(Surface{}, source, {std::make_shared<BoulderLayer>(options)});
    const Raster<float> heights = terrain.Evaluate();  // models are left out of the heights
    const std::vector<BoulderModel> models = terrain.Boulders(source->Grid()).models;
    // --8<-- [end:draw_boulders_as_relief_or_models]
    EXPECT_EQ(heights.Width(), 201u);
    ASSERT_GT(models.size(), 0u);
    for (const BoulderModel& model : models) {
        EXPECT_GE(model.boulder.diameter, 1.0);
    }
}

}  // namespace
}  // namespace terraingen
