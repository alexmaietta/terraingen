// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Reading and writing triangle meshes and instanced scenes as OBJ with MTL, glTF and STL.
#pragma once

#include "terraingen/mesh/mesh.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace terraingen {

/// A mesh's material: a name and a diffuse color.
struct MeshMaterial {
    std::string name = "default";                ///< Name in OBJ/MTL and glTF.
    std::array<double, 3> color{0.5, 0.5, 0.5};  ///< Linear RGB, each in [0, 1].

    /// Memberwise equality.
    bool operator==(const MeshMaterial& other) const {
        return name == other.name && color == other.color;
    }
    bool operator!=(const MeshMaterial& other) const {
        return !(*this == other);
    }
};

/// A mesh with optional normals and its material.
struct SceneMesh {
    TriangleMesh mesh;           ///< The mesh, in meters.
    std::vector<Vec3d> normals;  ///< Empty, or one unit normal per vertex.
    MeshMaterial material{};     ///< Written as an MTL material or a glTF base color.
};

/// An instance of a scene mesh: its vertices scaled per axis by `scale`, then moved by
/// `pose`, as glTF composes a node's scale, rotation and translation.
struct SceneNode {
    std::size_t mesh = 0;        ///< Index into the scene's meshes.
    Vec3d scale{1.0, 1.0, 1.0};  ///< Per-axis scale, nonzero and finite.
    RigidTransform pose{};       ///< Rotation and translation applied after the scale.
};

/// Whether `path` names a mesh format `ReadMesh` reads by its extension, in any case: .obj,
/// .gltf, .glb, .stl or .icq. `WriteScene` writes all but .icq (see `WriteIcq`).
bool IsMeshPath(const std::filesystem::path& path);

/// Writes meshes drawn once per node as glTF or OBJ (y-up, as (x, z, -y)) or binary STL (merged,
/// z-up, no materials), by extension.
/// @throws std::invalid_argument for another extension, or invalid meshes, nodes or poses.
/// @throws std::runtime_error if a file cannot be written.
void WriteScene(const std::filesystem::path& path, const std::vector<SceneMesh>& meshes,
                const std::vector<SceneNode>& nodes);

/// Writes one mesh, as `WriteScene` with a single node.
/// @throws std::invalid_argument and std::runtime_error as `WriteScene`.
void WriteMesh(const std::filesystem::path& path, const SceneMesh& mesh);

/// Writes `mesh` alone, without normals and with the default material.
/// @throws std::invalid_argument and std::runtime_error as `WriteScene`.
inline void WriteMesh(const std::filesystem::path& path, const TriangleMesh& mesh) {
    WriteMesh(path, SceneMesh{mesh, {}, {}});
}

/// Reads every triangle of an .obj, .gltf, .glb, .stl or .icq (kilometers) file in meters and
/// z-up, with the first material's diffuse color where there is one; normals are not read.
/// @throws std::invalid_argument for another extension.
/// @throws std::runtime_error if the file cannot be read or holds no triangles.
SceneMesh ReadMesh(const std::filesystem::path& path);

}  // namespace terraingen
