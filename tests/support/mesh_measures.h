// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Volumes, areas and icospheres of triangle meshes, for checking meshes in tests.
#pragma once

#include "terraingen/mesh/mesh.h"
#include "terraingen/mesh/polyhedra.h"

namespace terraingen::testing {

/// The volume a closed mesh encloses, as a sum of signed tetrahedra from the origin; negative
/// when the triangles face inward.
inline double Volume(const TriangleMesh& mesh) {
    ValidateMesh(mesh);
    double sum = 0.0;
    for (const auto& t : mesh.triangles) {
        sum += Dot(mesh.positions[t[0]], Cross(mesh.positions[t[1]], mesh.positions[t[2]]));
    }
    return sum / 6.0;
}

/// The total area of a mesh's triangles.
inline double SurfaceArea(const TriangleMesh& mesh) {
    ValidateMesh(mesh);
    double sum = 0.0;
    for (const auto& t : mesh.triangles) {
        sum += Length(
            TriangleAreaNormal(mesh.positions[t[0]], mesh.positions[t[1]], mesh.positions[t[2]]));
    }
    return 0.5 * sum;
}

/// A geodesic sphere of unit radius: the icosahedron subdivided `levels` times with every vertex
/// on the sphere.
inline TriangleMesh Icosphere(int levels) {
    return Subdivide(BaseSolidMesh(BaseSolid::Icosahedron), levels, true);
}

}  // namespace terraingen::testing
