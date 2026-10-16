/// @file
/// @brief Base solids for boulders, midpoint subdivision and icospheres.
#pragma once

#include "terraingen/mesh/mesh.h"

namespace terraingen {

/// @brief The convex solids boulders start from; each holds the origin inside it, so every ray
/// from the origin crosses its surface once.
enum class BaseSolid {
    Icosahedron,  ///< Twenty equilateral triangles with vertices on the unit sphere.
    Cube,         ///< [-1, 1]^3.
    Pyramid,      ///< A square base [-1, 1]^2 at z = -1 under an apex at (0, 0, 1).
    Prism,        ///< A wedge: the triangle (y, z) = (-1, -1), (1, -1), (0, 1) along x in [-1, 1].
    Tetrahedron,  ///< The regular tetrahedron with vertices (1, 1, 1), (1, -1, -1), (-1, 1, -1),
                  ///< (-1, -1, 1).
};

/// @brief A base solid as a closed, outward-facing triangle mesh, square faces split in two.
/// @param solid The solid.
/// @return The mesh.
/// @throws std::invalid_argument for a value outside the enum.
TriangleMesh BaseSolidMesh(BaseSolid solid);

/// @brief A mesh with each triangle split into four at its edges' midpoints, repeatedly.
///
/// A closed mesh stays closed, and a coarser level's vertices are a prefix of every finer level's.
/// @param mesh The mesh to split.
/// @param levels Times to split, >= 0.
/// @param onUnitSphere Whether each new vertex is pushed out to the unit sphere.
/// @return The subdivided mesh.
/// @throws std::invalid_argument for a negative `levels` or one that would exceed 2^32 vertices.
TriangleMesh Subdivide(const TriangleMesh& mesh, int levels, bool onUnitSphere = false);

/// @brief A geodesic sphere of unit radius: the icosahedron subdivided with every vertex on the
/// sphere.
/// @param levels Times to subdivide, 0 to 12.
/// @return A mesh of 10 * 4^`levels` + 2 vertices and 20 * 4^`levels` triangles.
/// @throws std::invalid_argument for a negative `levels` or one beyond 12.
TriangleMesh Icosphere(int levels);

}  // namespace terraingen
