// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Upsample a Shape Model
///
/// A shape model is the body and also its terrain's source. On its own the body interpolates its
/// labels bilinearly, which leaves flat facets between them. A `ShapeModelSource` treats the
/// labels as posts and refines them with any upsampler, as a DEM's cells are refined. Its heights
/// are the refined radius less the bilinear one, 0 on every label. Kernel upsamplers such as cubic
/// or Lanczos smooth between the labels. Diamond-square adds fractal detail below their spacing,
/// with a roughness fitted to them unless given. Each cube face's posts are read across its edges
/// and the faces are blended within half a label of them, so there are no seams. Layers then add
/// to the refined shape, and treat it as measured data that they need not add again.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <cmath>
#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, UpsampleAShapeModel) {
    const Surface body = Surface::ShapeModel(
        std::make_shared<RadialShape>(RadialShape::Sample(16, [](const Vec3d& u) {
            return 220.0 + 80.0 * u.x * u.x + 12.0 * std::sin(9.0 * u.y) +
                   8.0 * std::cos(7.0 * u.z);
        })));
    // --8<-- [start:upsample_a_shape_model]
    const auto cubic = std::make_shared<ShapeModelSource>(body);  // cubic between the labels
    const auto lanczos = std::make_shared<ShapeModelSource>(
        body, std::make_shared<KernelUpsampler>(ResampleMethod::Lanczos3));
    const auto rough = std::make_shared<ShapeModelSource>(
        body, std::make_shared<DiamondSquareUpsampler>());  // roughness fitted to the labels
    TerrainGenerator terrain;
    terrain.surface = body;
    terrain.source = rough;
    const RasterGrid site = RasterGrid::Covering({20.0, 10.0, 30.0, 20.0}, 0.05);  // degrees
    const Raster<float> heights = Evaluate(terrain, site, 0.25);  // meters above the bilinear shape
    // --8<-- [end:upsample_a_shape_model]
    EXPECT_NEAR(cubic->HeightAt(0.0, 0.0, 1.0), 0.0f, 1e-3f);
    EXPECT_NE(cubic->HeightAt(17.0, 23.0, 0.1), lanczos->HeightAt(17.0, 23.0, 0.1));
    EXPECT_EQ(heights.Width(), 200u);
}

}  // namespace
}  // namespace terraingen
