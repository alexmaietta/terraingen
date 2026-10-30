// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `BodyMesh` and `BodyIcq`, the whole body a terrain on a sphere or ellipsoid lies on.
#pragma once

#include "terraingen/mesh/io/icq.h"
#include "terraingen/mesh/mesh.h"
#include "terraingen/pipeline/terrain.h"

#include <cstddef>

namespace terraingen {

/// A mesh of the whole body a terrain on a sphere or ellipsoid lies on.
///
/// The six faces of a cube are each split into `cellsPerFace` x `cellsPerFace` quads along
/// equiangular coordinates (as `CubeSphereCells`), two triangles each, with every vertex at the
/// body's radius in that direction plus the terrain's height there (`HeightAt`; points without
/// data are on the datum). Positions are body-fixed meters, x toward longitude 0 on the equator
/// and z north (`Surface::Position`); vertices along the faces' edges are repeated per face.
/// @param spacing Sample spacing in meters; 0 for a quad's side at a face's center.
/// @param threads Threads to use; 0 for every hardware thread.
/// @throws std::invalid_argument for a terrain not on a body, zero cells, or a negative or
/// non-finite spacing; and whatever the terrain's parts throw.
TriangleMesh BodyMesh(const Terrain& terrain, std::size_t cellsPerFace, double spacing = 0.0,
                      unsigned threads = 0);

/// The whole body a terrain on a sphere or ellipsoid lies on as an ICQ shape model with `q`
/// intervals per face edge: each label at `IcqDirection`, at the body's radius in that direction
/// plus the terrain's height there (points without data are on the datum).
/// @param spacing Sample spacing in meters; 0 for an interval at a face's center.
/// @param threads Threads to use; 0 for every hardware thread.
/// @throws std::invalid_argument for a terrain not on a body, a `q` of 0, or a negative or
/// non-finite spacing; and whatever the terrain's parts throw.
IcqModel BodyIcq(const Terrain& terrain, std::size_t q, double spacing = 0.0, unsigned threads = 0);

}  // namespace terraingen
