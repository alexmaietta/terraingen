// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Generate a Standalone Boulder Shape
///
/// A boulder shape is drawn from a seed alone. It takes a style picked by weight (angular, platy,
/// rounded, blocks, wedges and more) and is stretched to its axes, cut by fractures, roughened and
/// worn. It comes in unit form, its long axis 1 along x, to be scaled and placed by the caller.
/// More subdivisions give the same shape in finer detail.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

namespace terraingen {
namespace {

TEST_F(SnippetTest, GenerateABoulderShape) {
    const std::filesystem::path path = OutputFolder() / "boulder.obj";

    // --8<-- [start:generate_a_boulder_shape]
    BoulderShapeOptions options;
    options.subdivisions = 4;
    const BoulderShape shape = GenerateBoulderShape(7, options);  // the same seed, the same shape
    WriteMesh(path, shape.mesh);
    // --8<-- [end:generate_a_boulder_shape]
    EXPECT_GT(shape.mesh.triangles.size(), 0u);
    EXPECT_TRUE(std::filesystem::exists(path));
}

}  // namespace
}  // namespace terraingen
