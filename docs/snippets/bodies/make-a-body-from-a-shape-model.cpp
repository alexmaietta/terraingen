// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Make a Body From a Shape Model File
///
/// A shape model makes a whole star-shaped body such as an asteroid. Points on it are
/// planetocentric longitude and latitude in degrees, and heights are measured along the direction
/// from its center. An ICQ model, such as Gaskell's for Itokawa or Eros from the PDS Small Bodies
/// Node, keeps its own Q. A closed mesh (.obj, .gltf, .glb or .stl) around its origin is resampled
/// along rays from the origin at Q intervals per cube face edge. Where a ray crosses the surface
/// more than once, the outermost crossing is kept.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, MakeABodyFromAShapeModel) {
    const std::filesystem::path folder = OutputFolder();
    const IcqModel rock = ToIcq(RadialShape::Sample(
        16, [](const Vec3d& u) { return 300.0 + 40.0 * u.x * u.x + 20.0 * u.z; }));
    WriteIcq(folder / "rock.icq", rock);
    WriteMesh(folder / "rock.obj", IcqToMesh(rock));
    // --8<-- [start:make_a_body_from_a_shape_model]
    const IcqModel model = ReadIcq(folder / "rock.icq");  // Gaskell's ICQ models are in kilometers
    const Surface body = Surface::ShapeModel(std::make_shared<RadialShape>(ToRadialShape(model)));
    const TriangleMesh mesh = ReadMesh(folder / "rock.obj").mesh;  // closed, in meters
    const Surface fromMesh =
        Surface::ShapeModel(std::make_shared<RadialShape>(ShapeModelFromMesh(mesh, 64)));
    // --8<-- [end:make_a_body_from_a_shape_model]
    EXPECT_TRUE(body.IsBody());
    EXPECT_FALSE(body.IsSphere());
    EXPECT_NEAR(body.MaxRadius(), fromMesh.MaxRadius(), 3.0);
}

}  // namespace
}  // namespace terraingen
