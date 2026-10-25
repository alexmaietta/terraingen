// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/mesh/io/mesh_io.h"

#include "mesh/io/formats.h"
#include "support/mesh_measures.h"
#include "support/temp_path.h"
#include "terraingen/mesh/polyhedra.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using terraingen::MeshMaterial;
using terraingen::RigidTransform;
using terraingen::SceneMesh;
using terraingen::SceneNode;
using terraingen::TriangleMesh;
using terraingen::Vec3d;
using terraingen::testing::TempPath;

namespace {

std::string Read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::uint32_t Word(const std::string& bytes, std::size_t offset) {
    std::uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, 4);
    return value;
}

/// Expects `read` to hold `expected`'s vertices, to float precision, and its triangle count.
void ExpectSameMesh(const TriangleMesh& read, const std::vector<Vec3d>& expected,
                    std::size_t triangles) {
    ASSERT_EQ(read.positions.size(), expected.size());
    EXPECT_EQ(read.triangles.size(), triangles);
    for (std::size_t v = 0; v < expected.size(); ++v) {
        EXPECT_NEAR(read.positions[v].x, expected[v].x, 1e-5) << v;
        EXPECT_NEAR(read.positions[v].y, expected[v].y, 1e-5) << v;
        EXPECT_NEAR(read.positions[v].z, expected[v].z, 1e-5) << v;
    }
}

/// `mesh`'s vertices placed by `node`, as `WriteScene` places them.
std::vector<Vec3d> PlacedVertices(const TriangleMesh& mesh, const SceneNode& node) {
    std::vector<Vec3d> placed;
    for (const Vec3d& p : mesh.positions) {
        placed.push_back(
            node.pose.Apply({p.x * node.scale.x, p.y * node.scale.y, p.z * node.scale.z}));
    }
    return placed;
}

}  // namespace

TEST(IsMeshPath, KnowsEveryMeshFormat) {
    for (const char* path : {"out/b.glb", "b.gltf", "b.OBJ", "b.stl", "shape.ICQ"}) {
        EXPECT_TRUE(terraingen::IsMeshPath(path)) << path;
    }
    EXPECT_FALSE(terraingen::IsMeshPath("b.ply"));
    EXPECT_FALSE(terraingen::IsMeshPath(""));
}

TEST(WriteMesh, WritesABinaryStlAndReadsItBackWelded) {
    const auto cube = terraingen::BaseSolidMesh(terraingen::BaseSolid::Cube);
    const auto path = TempPath("cube.stl");
    terraingen::WriteMesh(path, cube);
    const std::string bytes = Read(path);
    EXPECT_EQ(bytes.substr(0, 10), "TerrainGen");
    EXPECT_EQ(Word(bytes, 80), 12u);
    EXPECT_EQ(bytes.size(), 84u + 12u * 50u);
    const TriangleMesh read = terraingen::ReadMesh(path).mesh;
    EXPECT_EQ(read.positions.size(), 8u) << "shared corners joined";
    EXPECT_EQ(read.triangles.size(), 12u);
    EXPECT_NEAR(terraingen::testing::Volume(read), terraingen::testing::Volume(cube), 1e-6)
        << "still outward";
}

TEST(ReadMesh, ReadsAsciiStl) {
    const auto path = TempPath("facet.stl");
    std::ofstream(path) << "solid facet\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\n"
                           "vertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\nendsolid facet\n";
    const TriangleMesh read = terraingen::ReadMesh(path).mesh;
    ExpectSameMesh(read, {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, 1);
}

TEST(WriteMesh, RoundTripsThroughObjAndGltfInZUp) {
    TriangleMesh tilted;
    tilted.positions = {{0, 0, 2}, {1, 0, 2}, {0, 1, 2.5}};
    tilted.triangles = {{0, 1, 2}};
    const MeshMaterial basalt{"basalt", {0.2, 0.25, 0.3}};
    for (const char* name : {"tilted.obj", "tilted.gltf", "tilted.glb"}) {
        const auto path = TempPath(name);
        terraingen::WriteMesh(path, {tilted, terraingen::VertexNormals(tilted), basalt});
        const SceneMesh read = terraingen::ReadMesh(path);
        ExpectSameMesh(read.mesh, tilted.positions, 1);
        EXPECT_EQ(read.material.name, "basalt") << name;
        for (std::size_t c = 0; c < 3; ++c) {
            EXPECT_NEAR(read.material.color[c], basalt.color[c], 1e-6) << name;
        }
    }
    EXPECT_TRUE(std::filesystem::exists(TempPath("tilted.mtl")));
    EXPECT_TRUE(std::filesystem::exists(TempPath("tilted.bin")));
}

TEST(WriteMesh, WritesAValidBinaryGltfContainer) {
    const auto sphere = terraingen::testing::Icosphere(2);
    const auto path = TempPath("sphere.glb");
    terraingen::WriteMesh(path, sphere);
    const std::string bytes = Read(path);
    ASSERT_GE(bytes.size(), 28u);
    EXPECT_EQ(bytes.substr(0, 4), "glTF");
    EXPECT_EQ(Word(bytes, 4), 2u);
    EXPECT_EQ(Word(bytes, 8), bytes.size());
    EXPECT_EQ(bytes.substr(16, 4), "JSON");
    const std::string json = bytes.substr(20, Word(bytes, 12));
    EXPECT_NE(json.find("\"TerrainGen\""), std::string::npos);
    EXPECT_EQ(json.find("\"NORMAL\""), std::string::npos);
    EXPECT_THROW(terraingen::WriteMesh(path, {TriangleMesh{}, {}, {}}), std::invalid_argument);
    EXPECT_THROW(terraingen::WriteMesh(TempPath("sphere.ply"), sphere), std::invalid_argument);
    EXPECT_THROW(terraingen::WriteMesh(path, {sphere, {{0, 0, 1}}, {}}), std::invalid_argument);
    EXPECT_THROW(terraingen::WriteMesh(TempPath("missing/dir/x.glb"), sphere), std::runtime_error);
    EXPECT_THROW(terraingen::WriteMesh(TempPath("sphere.icq"), sphere), std::invalid_argument);
}

TEST(WriteMesh, WritesBinaryGltfForAnUpperCaseExtension) {
    const auto path = TempPath("upper.GLB");
    std::filesystem::remove(TempPath("upper.bin"));
    terraingen::WriteMesh(path, {terraingen::testing::Icosphere(1), {}, {}});
    EXPECT_EQ(Read(path).substr(0, 4), "glTF");
    EXPECT_FALSE(std::filesystem::exists(TempPath("upper.bin")));
    EXPECT_EQ(terraingen::ReadMesh(path).mesh.triangles.size(), 80u);
}

TEST(WriteScene, PlacesEveryNodeAlikeInEveryFormat) {
    const auto cube = terraingen::BaseSolidMesh(terraingen::BaseSolid::Cube);
    const auto pyramid = terraingen::BaseSolidMesh(terraingen::BaseSolid::Pyramid);
    const double pi = std::acos(-1.0);
    const std::vector<SceneNode> nodes{
        {0, {2.0, 3.0, 4.0}, RigidTransform::Translation({1.0, 2.0, 3.0})},
        {1, {1.0, 1.0, 1.0}, RigidTransform::RotationZ(pi / 2.0)},
        {0, {1.0, 1.0, 1.0}, RigidTransform::RotationX(pi).After(RigidTransform::RotationY(pi))}};
    std::vector<Vec3d> expected;
    std::size_t triangles = 0;
    const std::vector<TriangleMesh> meshes{cube, pyramid};
    for (const SceneNode& node : nodes) {
        const auto placed = PlacedVertices(meshes[node.mesh], node);
        expected.insert(expected.end(), placed.begin(), placed.end());
        triangles += meshes[node.mesh].triangles.size();
    }
    const std::vector<SceneMesh> scene{{cube, {}, {}},
                                       {pyramid, terraingen::VertexNormals(pyramid), {}}};
    for (const char* name : {"scene.obj", "scene.gltf", "scene.glb"}) {
        const auto path = TempPath(name);
        terraingen::WriteScene(path, scene, nodes);
        ExpectSameMesh(terraingen::ReadMesh(path).mesh, expected, triangles);
    }
    const auto stl = TempPath("scene.stl");
    terraingen::WriteScene(stl, scene, nodes);
    const TriangleMesh merged = terraingen::ReadMesh(stl).mesh;
    EXPECT_EQ(merged.triangles.size(), triangles);
    for (const Vec3d& p : merged.positions) {
        EXPECT_TRUE(std::any_of(expected.begin(), expected.end(), [&](const Vec3d& q) {
            return std::fabs(p.x - q.x) + std::fabs(p.y - q.y) + std::fabs(p.z - q.z) < 1e-5;
        })) << "every STL vertex is a placed vertex, z-up";
    }
    const auto path = TempPath("bad.glb");
    EXPECT_THROW(terraingen::WriteScene(path, {}, nodes), std::invalid_argument);
    EXPECT_THROW(terraingen::WriteScene(path, {{cube, {}, {}}}, {{3}}), std::invalid_argument);
    EXPECT_THROW(terraingen::WriteScene(path, {{cube, {}, {}}}, {{0, {0.0, 1.0, 1.0}, {}}}),
                 std::invalid_argument);
    RigidTransform mirror;
    mirror.rotation[0] = -1.0;
    EXPECT_THROW(terraingen::WriteScene(path, {{cube, {}, {}}}, {{0, {1.0, 1.0, 1.0}, mirror}}),
                 std::invalid_argument);
}

TEST(ReadMesh, RejectsOtherFormatsAndMissingFiles) {
    EXPECT_THROW(terraingen::ReadMesh(TempPath("rock.ply")), std::invalid_argument);
    EXPECT_THROW(terraingen::ReadMesh(TempPath("missing.obj")), std::runtime_error);
    EXPECT_THROW(terraingen::ReadMesh(TempPath("missing.glb")), std::runtime_error);
    const auto empty = TempPath("empty.obj");
    std::ofstream(empty) << "# nothing\n";
    EXPECT_THROW(terraingen::ReadMesh(empty), std::runtime_error);
}

TEST(ReadMesh, TriangulatesObjPolygons) {
    const auto path = TempPath("quad.obj");
    std::ofstream(path) << "v 0 0 0\nv 1 0 0\nv 1 0 -1\nv 0 0 -1\nf 1 2 3 4\n";
    const SceneMesh read = terraingen::ReadMesh(path);
    ExpectSameMesh(read.mesh, {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}, 2);
    EXPECT_EQ(read.material, MeshMaterial{}) << "no .mtl keeps the default material";
}

TEST(RotationQuaternion, CoversEveryBranchOfShepperdsMethod) {
    using terraingen::detail::RotationQuaternion;
    const auto expect = [](const std::array<double, 4>& q, const std::array<double, 4>& want) {
        for (std::size_t i = 0; i < 4; ++i) {
            EXPECT_NEAR(std::fabs(q[i]), std::fabs(want[i]), 1e-12) << i;
        }
    };
    const double pi = std::acos(-1.0);
    const double h = std::sqrt(0.5);
    expect(RotationQuaternion(RigidTransform{}.rotation), {0, 0, 0, 1});
    expect(RotationQuaternion(RigidTransform::RotationX(pi).rotation), {1, 0, 0, 0});
    expect(RotationQuaternion(RigidTransform::RotationY(pi).rotation), {0, 1, 0, 0});
    expect(RotationQuaternion(RigidTransform::RotationZ(pi).rotation), {0, 0, 1, 0});
    expect(RotationQuaternion(RigidTransform::RotationZ(pi / 2).rotation), {0, 0, h, h});
}
