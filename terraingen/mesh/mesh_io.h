/// @file
/// @brief Writers for triangle meshes and instanced scenes as PLY and binary glTF.
#pragma once

#include "terraingen/mesh/mesh.h"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace terraingen {

/// Writes a mesh as a binary little-endian PLY file, float32 positions and normals and
/// uint32 indices.
/// @param normals Empty, or one unit normal per vertex.
/// @throws std::invalid_argument for normals that are neither empty nor one per vertex, or a
/// triangle indexing past the vertices.
/// @throws std::runtime_error if the file cannot be written.
void WritePly(const std::filesystem::path& path, const TriangleMesh& mesh,
              const std::vector<Vec3d>& normals = {});

/// Writes a mesh as a binary glTF 2.0 file (.glb) with one node and one triangle primitive,
/// converted from z-up to glTF's y-up frame.
/// @param mesh The mesh, in metres.
/// @param normals Empty, or one unit normal per vertex.
/// @throws std::invalid_argument for an empty mesh, normals that are neither empty nor one per
/// vertex, or a triangle indexing past the vertices.
/// @throws std::runtime_error if the file cannot be written.
void WriteGlb(const std::filesystem::path& path, const TriangleMesh& mesh,
              const std::vector<Vec3d>& normals = {});

/// A mesh of a glTF scene.
struct SceneMesh {
    TriangleMesh mesh;           ///< The mesh, in metres.
    std::vector<Vec3d> normals;  ///< Empty, or one unit normal per vertex.
};

/// An instance of a scene mesh: its vertices scaled per axis by `scale`, then moved by
/// `pose`, as glTF composes a node's scale, rotation and translation.
struct SceneNode {
    std::size_t mesh = 0;        ///< Index into the scene's meshes.
    Vec3d scale{1.0, 1.0, 1.0};  ///< Per-axis scale, nonzero and finite.
    RigidTransform pose{};       ///< Rotation and translation applied after the scale.
};

/// Writes a binary glTF 2.0 file (.glb) of meshes drawn once per node, so each shared
/// shape is stored once; converted to y-up as `WriteGlb` is.
/// @throws std::invalid_argument for no meshes or nodes, an empty mesh, mismatched normals, a
/// triangle indexing past its vertices, a node naming no mesh, a scale that is zero or not finite,
/// or a pose whose rotation is not a rotation.
/// @throws std::runtime_error if the file cannot be written.
void WriteGlbScene(const std::filesystem::path& path, const std::vector<SceneMesh>& meshes,
                   const std::vector<SceneNode>& nodes);

}  // namespace terraingen
