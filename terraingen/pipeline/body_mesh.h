/// @file
/// @brief `BodyMesh`, a mesh of the whole body a terrain on a sphere lies on.
#pragma once

#include "terraingen/mesh/mesh.h"
#include "terraingen/pipeline/terrain.h"

#include <cstddef>

namespace terraingen {

/// @brief A mesh of the whole body a terrain on a sphere lies on.
///
/// The six faces of a cube are each split into `cellsPerFace` x `cellsPerFace` quads along
/// equiangular coordinates (as `CubeSphereCells`), two triangles each, with every vertex at the
/// body's radius plus the terrain's height there (`EvaluateAt`; points without data are on the
/// datum). Positions are body-fixed metres, x towards longitude 0 on the equator and z north
/// (`Surface::Position`); vertices along the faces' edges are repeated per face. Filters read
/// neighbouring cells, so they are left out.
/// @param terrain The terrain, on a sphere.
/// @param cellsPerFace Quads along each edge of a face.
/// @param spacing Sample spacing in metres; 0 for a quad's side at a face's centre.
/// @param threads Threads to use; 0 for every hardware thread.
/// @return The mesh.
/// @throws std::invalid_argument for a terrain not on a sphere, zero cells, or a negative or
/// non-finite spacing; and whatever the terrain's parts throw.
TriangleMesh BodyMesh(const Terrain& terrain, std::size_t cellsPerFace, double spacing = 0.0,
                      unsigned threads = 0);

}  // namespace terraingen
