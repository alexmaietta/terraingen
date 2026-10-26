// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Base solids for boulders and midpoint subdivision.
#pragma once

#include "terraingen/mesh/mesh.h"

namespace terraingen {

/// The convex solids boulders start from; each holds the origin inside it, so every ray
/// from the origin crosses its surface once.
enum class BaseSolid {
    Icosahedron,  ///< Twenty equilateral triangles with vertices on the unit sphere.
    Cube,         ///< [-1, 1]^3.
    Pyramid,      ///< A square base [-1, 1]^2 at z = -1 under an apex at (0, 0, 1).
    Prism,        ///< A wedge: the triangle (y, z) = (-1, -1), (1, -1), (0, 1) along x in [-1, 1].
    Tetrahedron,  ///< The regular tetrahedron with vertices (1, 1, 1), (1, -1, -1), (-1, 1, -1),
                  ///< (-1, -1, 1).
};

/// A base solid as a closed, outward-facing triangle mesh, square faces split in two.
/// @throws std::invalid_argument for a value outside the enum.
TriangleMesh BaseSolidMesh(BaseSolid solid);

/// A mesh with each triangle split into four at its edges' midpoints, `levels` times; a coarser
/// level's vertices are a prefix of every finer level's.
/// @param onUnitSphere Whether each new vertex is pushed out to the unit sphere.
/// @throws std::invalid_argument for a negative `levels` or one that would exceed 2^32 vertices.
TriangleMesh Subdivide(const TriangleMesh& mesh, int levels, bool onUnitSphere = false);

}  // namespace terraingen
