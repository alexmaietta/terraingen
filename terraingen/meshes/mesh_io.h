#pragma once

#include "terraingen/meshes/mesh.h"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace terraingen {

/// Writes `mesh` as a binary little-endian PLY file: float32 vertex positions, with float32 normals
/// when `normals` holds one per vertex, and triangles as lists of uint32 indices.
///
/// Reference: G. Turk, "The PLY Polygon File Format", 1994.
/// http://paulbourke.net/dataformats/ply/
/// @throws std::invalid_argument for normals that are neither empty nor one per vertex, or a
/// triangle indexing past the vertices.
/// @throws std::runtime_error if the file cannot be written.
void WritePly(const std::filesystem::path& path, const TriangleMesh& mesh,
              const std::vector<Vec3d>& normals = {});

/// Writes `mesh` as a binary glTF 2.0 file (.glb) with one node and one triangle primitive, with
/// normals when `normals` holds one per vertex. glTF is y-up, so a point (x, y, z) in TerrainGen's
/// z-up frame is written as (x, z, -y), in metres.
///
/// Reference: The Khronos Group, "glTF 2.0 Specification", 2021.
/// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
/// @throws std::invalid_argument for an empty mesh, normals that are neither empty nor one per
/// vertex, or a triangle indexing past the vertices.
/// @throws std::runtime_error if the file cannot be written.
void WriteGlb(const std::filesystem::path& path, const TriangleMesh& mesh,
              const std::vector<Vec3d>& normals = {});

/// A mesh of a glTF scene, with normals when they hold one per vertex.
struct SceneMesh {
    TriangleMesh mesh;
    std::vector<Vec3d> normals;
};

/// An instance of a scene mesh: the mesh's vertices scaled per axis by `scale`, then moved by
/// `pose`, as glTF composes a node's scale, rotation and translation.
struct SceneNode {
    std::size_t mesh = 0;  ///< Index into the scene's meshes.
    Vec3d scale{1.0, 1.0, 1.0};
    RigidTransform pose{};
};

/// Writes a binary glTF 2.0 file (.glb) of `meshes` drawn once per node of `nodes`, each with its
/// own scale, rotation and translation, so a field of boulders sharing a few shapes is stored once
/// per shape. Converted to glTF's y-up frame as `WriteGlb` is.
///
/// Reference: The Khronos Group, "glTF 2.0 Specification", 2021, sections 3.5 (nodes) and 3.7
/// (meshes). https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
/// @throws std::invalid_argument for no meshes or nodes, an empty mesh, mismatched normals, a
/// triangle indexing past its vertices, a node naming no mesh, a scale that is zero or not finite,
/// or a pose whose rotation is not a rotation.
/// @throws std::runtime_error if the file cannot be written.
void WriteGlbScene(const std::filesystem::path& path, const std::vector<SceneMesh>& meshes,
                   const std::vector<SceneNode>& nodes);

}  // namespace terraingen
