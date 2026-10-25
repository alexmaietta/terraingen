// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Write the Whole Body as a Mesh or Shape Model
///
/// `BodyMesh` meshes the whole body from the six faces of a cube, each split into cells per face
/// edge squared quads. Every vertex sits at the body's radius plus the terrain's height there,
/// in body-fixed meters with x toward 0 E and z north. `BodyIcq` samples the same terrain at the
/// labels of an ICQ shape model, which other shape-model tools read. It works on spheres and
/// ellipsoids as well as shape models.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, WriteTheWholeBodyAsAMesh) {
    const std::filesystem::path folder = OutputFolder();
    const Surface body = Surface::ShapeModel(std::make_shared<RadialShape>(RadialShape::Sample(
        32, [](const Vec3d& u) { return 220.0 + 80.0 * u.x * u.x + 15.0 * u.y * u.z; })));
    CraterLayerOptions options;
    options.seed = 6;
    options.generation.dMin = 3.0;
    options.generation.dMax = 40.0;
    TerrainGenerator terrain;
    terrain.surface = body;
    terrain.layers = {std::make_shared<CraterLayer>(DefaultCraterModels(), options, body)};
    // --8<-- [start:write_the_whole_body_as_a_mesh]
    const TriangleMesh mesh =
        BodyMesh(terrain, 128, 0.5);       // 6 x 128 x 128 quads, samples 0.5 m apart
    WriteMesh(folder / "body.glb", mesh);  // or .gltf, .obj or .stl
    const IcqModel shapeModel = BodyIcq(terrain, 128, 0.5);  // Q = 128
    WriteIcq(folder / "body.icq", shapeModel);               // in kilometers
    // --8<-- [end:write_the_whole_body_as_a_mesh]
    EXPECT_EQ(mesh.triangles.size(), 6u * 128u * 128u * 2u);
    EXPECT_EQ(ReadIcq(folder / "body.icq").q, 128u);
}

}  // namespace
}  // namespace terraingen
