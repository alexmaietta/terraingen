/// @file
/// @brief Triangle meshes, their measures, normals and rigid transforms.
#pragma once

#include "terraingen/util/geometry.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace terraingen {

/// A triangle mesh of shared vertices; triangles are counter-clockwise seen from outside,
/// so a closed mesh has a positive `Volume`.
struct TriangleMesh {
    std::vector<Vec3d> positions;                         ///< Vertex positions.
    std::vector<std::array<std::uint32_t, 3>> triangles;  ///< Indices into `positions`.
};

/// An axis-aligned box.
struct Box3d {
    Vec3d min;  ///< Lowest corner.
    Vec3d max;  ///< Highest corner.

    /// The box's size along each axis, `max` - `min`.
    Vec3d Extents() const {
        return max - min;
    }
};

/// The normal of the counter-clockwise triangle `a`, `b`, `c`, with a length of twice its area.
inline Vec3d TriangleAreaNormal(const Vec3d& a, const Vec3d& b, const Vec3d& c) {
    return Cross(b - a, c - a);
}

/// The smallest axis-aligned box around a mesh's vertices.
/// @throws std::invalid_argument for a mesh without vertices.
Box3d Bounds(const TriangleMesh& mesh);

/// Checks that every triangle indexes one of the mesh's vertices.
/// @throws std::invalid_argument for a triangle indexing past the vertices.
void ValidateMesh(const TriangleMesh& mesh);

/// The volume a closed mesh encloses, as a sum of signed tetrahedra from the origin; negative
/// when the triangles face inward.
/// @throws std::invalid_argument as `ValidateMesh`.
double Volume(const TriangleMesh& mesh);

/// The total area of a mesh's triangles.
/// @throws std::invalid_argument as `ValidateMesh`.
double SurfaceArea(const TriangleMesh& mesh);

/// The unit normal of each vertex: the area-weighted mean of its triangles' normals.
/// @throws std::invalid_argument as `ValidateMesh`.
std::vector<Vec3d> VertexNormals(const TriangleMesh& mesh);

/// A mesh with per-vertex normals, its vertices split along creases so fracture faces keep
/// hard edges while noisy surfaces stay smooth.
struct ShadedMesh {
    TriangleMesh mesh;           ///< The mesh, vertices split along creases.
    std::vector<Vec3d> normals;  ///< One per vertex of `mesh`.
};

/// A mesh with its vertices split along creases and normals averaged, weighted by area,
/// over the triangles that meet within the crease angle.
/// @param creaseAngle The largest angle between triangle normals that is smoothed, in radians.
/// @throws std::invalid_argument for a negative or non-finite `creaseAngle`, or as
/// `ValidateMesh`.
ShadedMesh WithCreases(const TriangleMesh& mesh, double creaseAngle);

/// A rotation followed by a translation.
struct RigidTransform {
    /// Row-major 3 x 3.
    std::array<double, 9> rotation{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    /// Applied after the rotation.
    Vec3d translation{};

    /// The point rotated, then translated.
    Vec3d Apply(const Vec3d& point) const;
    /// The vector rotated, without the translation.
    Vec3d Rotate(const Vec3d& vector) const;
    /// The transform that applies `first`, then this one.
    RigidTransform After(const RigidTransform& first) const;

    /// A rotation by `angle` radians about x, counter-clockwise looking down the axis.
    static RigidTransform RotationX(double angle);
    /// A rotation by `angle` radians about y, counter-clockwise looking down the axis.
    static RigidTransform RotationY(double angle);
    /// A rotation by `angle` radians about z, counter-clockwise looking down the axis.
    static RigidTransform RotationZ(double angle);
    /// A pure translation by `offset`.
    static RigidTransform Translation(const Vec3d& offset);
};

/// Checks that a matrix is a proper rotation: orthonormal to within 1e-9, without a
/// reflection.
/// @param rotation Row-major 3 x 3.
/// @throws std::invalid_argument otherwise.
void RequireRotation(const std::array<double, 9>& rotation);

/// A mesh with every vertex moved by a transform.
TriangleMesh Transformed(const TriangleMesh& mesh, const RigidTransform& transform);

/// Checks that each per-axis scale factor is finite and not zero.
/// @param what What the factors are, as error messages name them, such as "Node scales".
/// @throws std::invalid_argument otherwise.
void RequireScaleFactors(const Vec3d& scale, std::string_view what);

/// A mesh with every vertex scaled about the origin per axis; triangles flip when an odd
/// number of the factors are negative, so the mesh stays outward-facing.
/// @param scale The factor per axis, nonzero and finite.
/// @throws std::invalid_argument for a zero or non-finite factor.
TriangleMesh Scaled(const TriangleMesh& mesh, const Vec3d& scale);

}  // namespace terraingen
