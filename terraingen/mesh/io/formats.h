/// @file
/// @brief The format writers and readers behind `WriteScene` and `ReadMesh` (gltf.cpp, obj.cpp,
/// stl.cpp), and what they share.
#pragma once

#include "terraingen/mesh/io/mesh_io.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace terraingen::detail {

/// TerrainGen's z-up (x, y, z) in the y-up frame of OBJ and glTF: (x, z, -y).
inline Vec3d YUp(const Vec3d& p) {
    return {p.x, p.z, -p.y};
}

/// A y-up point back in TerrainGen's z-up frame.
inline Vec3d ZUp(const Vec3d& p) {
    return {p.x, -p.z, p.y};
}

/// A row-major rotation matrix in the y-up frame.
inline std::array<double, 9> YUp(const std::array<double, 9>& r) {
    return {r[0], r[2], -r[1], r[6], r[8], -r[7], -r[3], -r[5], r[4]};
}

/// `p` of mesh `node.mesh` placed by the node: scaled, then posed.
Vec3d Placed(const SceneNode& node, const Vec3d& p);

/// The normal `n` of a vertex of the node's mesh after `Placed`, unit length.
Vec3d PlacedNormal(const SceneNode& node, const Vec3d& n);

/// Writes `bytes` to `path`.
/// @throws std::runtime_error if the file cannot be written.
void Save(const std::filesystem::path& path, const std::string& bytes);

/// `WriteScene` for .obj, with the materials in an .mtl of the same stem beside it.
void WriteObj(const std::filesystem::path& path, const std::vector<SceneMesh>& meshes,
              const std::vector<SceneNode>& nodes);

/// `WriteScene` for .gltf (with a .bin beside it) or .glb.
void WriteGltf(const std::filesystem::path& path, const std::vector<SceneMesh>& meshes,
               const std::vector<SceneNode>& nodes);

/// `WriteScene` for .stl: every node's mesh placed and merged into one binary STL, z-up.
void WriteStl(const std::filesystem::path& path, const std::vector<SceneMesh>& meshes,
              const std::vector<SceneNode>& nodes);

/// `ReadMesh` for binary or ASCII .stl, corners at the same position joined into one vertex.
SceneMesh ReadStl(const std::filesystem::path& path);

/// `ReadMesh` for .obj.
SceneMesh ReadObj(const std::filesystem::path& path);

/// `ReadMesh` for .gltf and .glb.
SceneMesh ReadGltf(const std::filesystem::path& path);

}  // namespace terraingen::detail
