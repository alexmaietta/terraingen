#pragma once

#include "terraingen/meshes/mesh.h"

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

}  // namespace terraingen
