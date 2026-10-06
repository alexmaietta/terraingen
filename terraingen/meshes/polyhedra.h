#pragma once

#include "terraingen/meshes/mesh.h"

namespace terraingen {

/// The convex solids boulders start from. Each holds the origin inside it, so every ray from the
/// origin crosses its surface once and shapes moved along those rays cannot fold over themselves.
enum class BaseSolid {
    Icosahedron,  ///< Twenty equilateral triangles with vertices on the unit sphere.
    Cube,         ///< [-1, 1]^3.
    Pyramid,      ///< A square base [-1, 1]^2 at z = -1 under an apex at (0, 0, 1).
    Prism,        ///< A wedge: the triangle (y, z) = (-1, -1), (1, -1), (0, 1) along x in [-1, 1].
    Tetrahedron,  ///< The regular tetrahedron with vertices (1, 1, 1), (1, -1, -1), (-1, 1, -1),
                  ///< (-1, -1, 1).
};

/// `solid` as a closed triangle mesh facing outward, square faces split into two triangles.
TriangleMesh BaseSolidMesh(BaseSolid solid);

/// `mesh` with each triangle split into four at its edges' midpoints, `levels` times, as in Loop's
/// subdivision without its smoothing. Midpoints are shared between the triangles on either side of
/// an edge, so a closed mesh stays closed, and new vertices are appended after the old ones, so a
/// coarser level's vertices are a prefix of every finer level's: a shape computed per vertex agrees
/// across the levels of detail. With `onUnitSphere`, each new vertex is pushed out to the unit
/// sphere.
///
/// Reference: C. Loop, "Smooth Subdivision Surfaces Based on Triangles", M.S. thesis, University of
/// Utah, 1987 (the 1-to-4 split).
/// @throws std::invalid_argument for a negative `levels` or one that would exceed 2^32 vertices.
TriangleMesh Subdivide(const TriangleMesh& mesh, int levels, bool onUnitSphere = false);

/// A geodesic sphere of unit radius: the icosahedron subdivided `levels` times with every vertex on
/// the sphere, 10 * 4^levels + 2 vertices and 20 * 4^levels nearly equilateral triangles, which
/// take displacement more evenly than a latitude-longitude sphere.
///
/// Reference: J. R. Baumgardner and P. O. Frederickson, "Icosahedral discretization of the
/// two-sphere", SIAM Journal on Numerical Analysis 22(6):1107-1115, 1985.
/// https://doi.org/10.1137/0722066
/// @throws std::invalid_argument for a negative `levels` or one beyond 12.
TriangleMesh Icosphere(int levels);

}  // namespace terraingen
