// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Use Rock Meshes of Your Own for Boulders
///
/// Boulder shapes can come from rock meshes, such as scans, in place of the procedural styles.
/// Each boulder picks one by its shape seed, scaled to a long axis of 1 and then to its own size.
/// `ReadMesh` reads them from .obj, .gltf, .glb or .stl files; here an octahedron stands in. A
/// configuration names the files as a boulder layer's `shape_files`.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, UseRockMeshesForBoulders) {
    const std::filesystem::path path = OutputFolder() / "rock.obj";

    // --8<-- [start:use_rock_meshes_for_boulders]
    TriangleMesh octahedron;
    octahedron.positions = {{1, 0, 0},    {-1, 0, 0},  {0, 0.7, 0},
                            {0, -0.7, 0}, {0, 0, 0.5}, {0, 0, -0.5}};
    octahedron.triangles = {{0, 2, 4}, {2, 1, 4}, {1, 3, 4}, {3, 0, 4},
                            {2, 0, 5}, {1, 2, 5}, {3, 1, 5}, {0, 3, 5}};

    BoulderShapeOptions shapes;
    shapes.library = {std::make_shared<const SceneMesh>(SceneMesh{octahedron, {}, {}})};
    const BoulderShape shape = GenerateBoulderShape(7, shapes);  // scaled to a long axis of 1
    WriteMesh(path, shape.mesh);
    // --8<-- [end:use_rock_meshes_for_boulders]
    EXPECT_EQ(shape.mesh.triangles.size(), 8u);
    EXPECT_TRUE(std::filesystem::exists(path));
}

}  // namespace
}  // namespace terraingen
