#include "terraingen/meshes/mesh_io.h"

#include "terraingen/meshes/polyhedra.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

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

std::filesystem::path TempPath(const std::string& name) {
    return std::filesystem::path(::testing::TempDir()) / name;
}

}  // namespace

TEST(WritePly, WritesABinaryPlyWithOptionalNormals) {
    const auto cube = terraingen::BaseSolidMesh(terraingen::BaseSolid::Cube);
    const auto path = TempPath("cube.ply");
    terraingen::WritePly(path, cube);
    std::string bytes = Read(path);
    const std::string header = "ply\nformat binary_little_endian 1.0\ncomment TerrainGen\n"
                               "element vertex 8\nproperty float x\nproperty float y\n"
                               "property float z\nelement face 12\n"
                               "property list uchar uint vertex_indices\nend_header\n";
    ASSERT_EQ(bytes.substr(0, header.size()), header);
    EXPECT_EQ(bytes.size(), header.size() + 8 * 12 + 12 * 13);
    float first = 0.0f;
    std::memcpy(&first, bytes.data() + header.size(), 4);
    EXPECT_EQ(first, -1.0f);

    terraingen::WritePly(path, cube, terraingen::VertexNormals(cube));
    bytes = Read(path);
    EXPECT_NE(bytes.find("property float nz\n"), std::string::npos);
    EXPECT_THROW(terraingen::WritePly(path, cube, {{0, 0, 1}}), std::invalid_argument);
    auto broken = cube;
    broken.triangles[0][0] = 99;
    EXPECT_THROW(terraingen::WritePly(path, broken), std::invalid_argument);
    EXPECT_THROW(terraingen::WritePly(TempPath("missing/dir/x.ply"), cube), std::runtime_error);
}

TEST(WriteGlb, WritesAValidBinaryGltfContainer) {
    const auto sphere = terraingen::Icosphere(2);
    const auto shaded = terraingen::WithCreases(sphere, 0.3);
    const auto path = TempPath("sphere.glb");
    for (const bool normals : {false, true}) {
        if (normals) {
            terraingen::WriteGlb(path, shaded.mesh, shaded.normals);
        } else {
            terraingen::WriteGlb(path, sphere);
        }
        const std::string bytes = Read(path);
        ASSERT_GE(bytes.size(), 28u);
        EXPECT_EQ(bytes.substr(0, 4), "glTF");
        EXPECT_EQ(Word(bytes, 4), 2u);
        EXPECT_EQ(Word(bytes, 8), bytes.size());
        const std::uint32_t jsonLength = Word(bytes, 12);
        EXPECT_EQ(bytes.substr(16, 4), "JSON");
        EXPECT_EQ(jsonLength % 4, 0u);
        const std::string json = bytes.substr(20, jsonLength);
        EXPECT_NE(json.find(R"("version":"2.0")"), std::string::npos);
        EXPECT_EQ(json.find(R"("NORMAL")") != std::string::npos, normals);
        const std::size_t binary = 20 + jsonLength;
        EXPECT_EQ(bytes.substr(binary + 4, 3), std::string("BIN"));
        const auto& mesh = normals ? shaded.mesh : sphere;
        const std::size_t expected =
            mesh.positions.size() * 12 * (normals ? 2 : 1) + mesh.triangles.size() * 12;
        EXPECT_EQ(Word(bytes, binary), expected);
        EXPECT_NE(json.find(R"("byteLength":)" + std::to_string(expected)), std::string::npos);
    }
    float y = 0.0f;
    terraingen::TriangleMesh tilted;
    tilted.positions = {{0, 0, 2}, {1, 0, 2}, {0, 1, 2}};
    tilted.triangles = {{0, 1, 2}};
    terraingen::WriteGlb(path, tilted);
    const std::string bytes = Read(path);
    std::memcpy(&y, bytes.data() + 20 + Word(bytes, 12) + 8 + 4, 4);
    EXPECT_EQ(y, 2.0f) << "z up becomes y up";
    EXPECT_THROW(terraingen::WriteGlb(path, terraingen::TriangleMesh{}), std::invalid_argument);
}
