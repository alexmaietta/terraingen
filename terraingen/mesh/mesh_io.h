/// @file
/// @brief Reading and writing triangle meshes and instanced scenes as PLY, OBJ with MTL, and glTF.
#pragma once

#include "terraingen/mesh/mesh.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace terraingen {

/// A mesh's material: a name and a diffuse colour.
struct MeshMaterial {
    std::string name = "default";                 ///< Name in OBJ/MTL and glTF.
    std::array<double, 3> colour{0.5, 0.5, 0.5};  ///< Linear RGB, each in [0, 1].

    bool operator==(const MeshMaterial&) const = default;
};

/// A mesh with optional normals and its material.
struct SceneMesh {
    TriangleMesh mesh;           ///< The mesh, in metres.
    std::vector<Vec3d> normals;  ///< Empty, or one unit normal per vertex.
    MeshMaterial material{};     ///< Written as an MTL material or a glTF base colour.
};

/// An instance of a scene mesh: its vertices scaled per axis by `scale`, then moved by
/// `pose`, as glTF composes a node's scale, rotation and translation.
struct SceneNode {
    std::size_t mesh = 0;        ///< Index into the scene's meshes.
    Vec3d scale{1.0, 1.0, 1.0};  ///< Per-axis scale, nonzero and finite.
    RigidTransform pose{};       ///< Rotation and translation applied after the scale.
};

/// Whether `path` names a mesh format by its extension: .ply, .obj, .gltf or .glb.
bool IsMeshPath(const std::filesystem::path& path);

/// Writes meshes drawn once per node, in the format the extension names. glTF (.glb, or .gltf
/// with a .bin beside it) stores each mesh once and instances it; OBJ (with an .mtl beside it)
/// writes one object per node, and PLY one merged mesh. OBJ and glTF are y-up: TerrainGen's z-up
/// (x, y, z) is written as (x, z, -y).
/// @throws std::invalid_argument for another extension, no meshes or nodes, an empty or invalid
/// mesh, mismatched normals, a node naming no mesh, a scale that is zero or not finite, or a pose
/// whose rotation is not a rotation.
/// @throws std::runtime_error if a file cannot be written.
void WriteScene(const std::filesystem::path& path, const std::vector<SceneMesh>& meshes,
                const std::vector<SceneNode>& nodes);

/// Writes one mesh, as `WriteScene` with a single node.
/// @throws std::invalid_argument and std::runtime_error as `WriteScene`.
void WriteMesh(const std::filesystem::path& path, const SceneMesh& mesh);

/// Reads every triangle of an .obj (polygons triangulated) or a .gltf or .glb (every triangle
/// primitive of the default scene, with its nodes' transforms), converted to z-up, with the first
/// material's diffuse colour. Normals are not read.
/// @throws std::invalid_argument for another extension.
/// @throws std::runtime_error if the file cannot be read or holds no triangles.
SceneMesh ReadMesh(const std::filesystem::path& path);

}  // namespace terraingen
