/// @file
/// @brief Triangle meshes, their measures, normals and rigid transforms.
#pragma once

#include "terraingen/util/geometry.h"

#include <array>
#include <cstdint>
#include <vector>

namespace terraingen {

/// @brief A triangle mesh of shared vertices; triangles are counter-clockwise seen from outside,
/// so a closed mesh has a positive `Volume`.
struct TriangleMesh {
    std::vector<Vec3d> positions;                         ///< Vertex positions.
    std::vector<std::array<std::uint32_t, 3>> triangles;  ///< Indices into `positions`.
};

/// @brief An axis-aligned box.
struct Box3d {
    Vec3d min;  ///< Lowest corner.
    Vec3d max;  ///< Highest corner.

    /// @brief The box's size along each axis.
    /// @return `max` - `min`.
    Vec3d Extents() const {
        return max - min;
    }
};

/// @brief The smallest axis-aligned box around a mesh's vertices.
/// @param mesh The mesh.
/// @return The box.
/// @throws std::invalid_argument for a mesh without vertices.
Box3d Bounds(const TriangleMesh& mesh);

/// @brief The volume a closed mesh encloses, as a sum of signed tetrahedra from the origin.
/// @param mesh The mesh.
/// @return The volume; negative when the triangles face inward.
/// @throws std::out_of_range for a triangle indexing past the vertices.
double Volume(const TriangleMesh& mesh);

/// @brief The total area of a mesh's triangles.
/// @param mesh The mesh.
/// @return The area.
/// @throws std::out_of_range for a triangle indexing past the vertices.
double SurfaceArea(const TriangleMesh& mesh);

/// @brief The unit normal of each vertex: the area-weighted mean of its triangles' normals.
/// @param mesh The mesh.
/// @return One normal per vertex.
/// @throws std::out_of_range for a triangle indexing past the vertices.
std::vector<Vec3d> VertexNormals(const TriangleMesh& mesh);

/// @brief A mesh with per-vertex normals, its vertices split along creases so fracture faces keep
/// hard edges while noisy surfaces stay smooth.
struct ShadedMesh {
    TriangleMesh mesh;           ///< The mesh, vertices split along creases.
    std::vector<Vec3d> normals;  ///< One per vertex of `mesh`.
};

/// @brief A mesh with its vertices split along creases and normals averaged, weighted by area,
/// over the triangles that meet within the crease angle.
/// @param mesh The mesh.
/// @param creaseAngle The largest angle between triangle normals that is smoothed, in radians.
/// @return The shaded mesh.
/// @throws std::invalid_argument for a negative or non-finite `creaseAngle`.
/// @throws std::out_of_range for a triangle indexing past the vertices.
ShadedMesh WithCreases(const TriangleMesh& mesh, double creaseAngle);

/// @brief A rotation followed by a translation.
struct RigidTransform {
    /// Row-major 3 x 3.
    std::array<double, 9> rotation{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    /// Applied after the rotation.
    Vec3d translation{};

    /// @brief Moves a point.
    /// @param point The point.
    /// @return The rotated, then translated point.
    Vec3d Apply(const Vec3d& point) const;
    /// @brief This transform composed after another.
    /// @param first The transform applied first.
    /// @return The transform that applies `first`, then this one.
    RigidTransform After(const RigidTransform& first) const;

    /// @brief A rotation about x, counter-clockwise looking down the axis.
    /// @param angle The angle, in radians.
    /// @return The rotation.
    static RigidTransform RotationX(double angle);
    /// @brief A rotation about y, counter-clockwise looking down the axis.
    /// @param angle The angle, in radians.
    /// @return The rotation.
    static RigidTransform RotationY(double angle);
    /// @brief A rotation about z, counter-clockwise looking down the axis.
    /// @param angle The angle, in radians.
    /// @return The rotation.
    static RigidTransform RotationZ(double angle);
    /// @brief A pure translation.
    /// @param offset The offset.
    /// @return The translation.
    static RigidTransform Translation(const Vec3d& offset);
};

/// @brief Checks that a matrix is a proper rotation: orthonormal to within 1e-9, without a
/// reflection.
/// @param rotation The matrix to check, row-major 3 x 3.
/// @throws std::invalid_argument otherwise.
void RequireRotation(const std::array<double, 9>& rotation);

/// @brief The unit quaternion of a rotation.
/// @param rotation A proper rotation, row-major 3 x 3.
/// @return The quaternion as (x, y, z, w).
std::array<double, 4> RotationQuaternion(const std::array<double, 9>& rotation);

/// @brief A mesh with every vertex moved by a transform.
/// @param mesh The mesh.
/// @param transform The transform.
/// @return The moved mesh.
TriangleMesh Transformed(const TriangleMesh& mesh, const RigidTransform& transform);

/// @brief A mesh with every vertex scaled about the origin per axis; triangles flip when an odd
/// number of the factors are negative, so the mesh stays outward-facing.
/// @param mesh The mesh.
/// @param scale The factor per axis, nonzero and finite.
/// @return The scaled mesh.
/// @throws std::invalid_argument for a zero or non-finite factor.
TriangleMesh Scaled(const TriangleMesh& mesh, const Vec3d& scale);

}  // namespace terraingen
