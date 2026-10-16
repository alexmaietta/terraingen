#pragma once

#include "terraingen/util/geometry.h"

#include <array>
#include <cstdint>
#include <vector>

namespace terraingen {

/// A triangle mesh: shared vertices and triangles that index them, counter-clockwise seen from
/// outside, so a closed mesh has a positive `Volume`.
struct TriangleMesh {
    std::vector<Vec3d> positions;
    std::vector<std::array<std::uint32_t, 3>> triangles;
};

/// The smallest axis-aligned box holding a set of points.
struct Box3d {
    Vec3d min;
    Vec3d max;

    Vec3d Extents() const {
        return max - min;
    }
};

/// The box around `mesh`'s vertices.
/// @throws std::invalid_argument for a mesh without vertices.
Box3d Bounds(const TriangleMesh& mesh);

/// The volume a closed mesh encloses, from the divergence theorem as a sum of signed tetrahedra
/// between the origin and each triangle; negative when the triangles face inward.
double Volume(const TriangleMesh& mesh);

/// The total area of the mesh's triangles.
double SurfaceArea(const TriangleMesh& mesh);

/// The unit normal of each vertex: the area-weighted mean of the normals of the triangles around
/// it.
std::vector<Vec3d> VertexNormals(const TriangleMesh& mesh);

/// A mesh with flat or smooth normals: vertices are split wherever the triangles around them meet
/// at more than `creaseAngle` radians, so fracture faces keep hard edges while noisy surfaces stay
/// smooth.
struct ShadedMesh {
    TriangleMesh mesh;
    std::vector<Vec3d> normals;  ///< One per vertex of `mesh`.
};

/// `mesh` with its vertices split along creases sharper than `creaseAngle` (radians), and with
/// normals averaged, weighted by area, over the triangles that meet within it.
/// @throws std::invalid_argument for a negative or non-finite `creaseAngle`.
ShadedMesh WithCreases(const TriangleMesh& mesh, double creaseAngle);

/// A rotation (row-major 3 x 3 matrix) followed by a translation.
struct RigidTransform {
    std::array<double, 9> rotation{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    Vec3d translation{};

    Vec3d Apply(const Vec3d& point) const;
    /// This transform after `first`: first's rotation and translation, then this one's.
    RigidTransform After(const RigidTransform& first) const;

    /// A rotation of `angle` radians, counter-clockwise looking down the axis, about x, y or z.
    static RigidTransform RotationX(double angle);
    static RigidTransform RotationY(double angle);
    static RigidTransform RotationZ(double angle);
    static RigidTransform Translation(const Vec3d& offset);
};

/// @brief Checks that `rotation` (row-major 3 x 3) is a proper rotation: orthonormal to within
/// 1e-9, without a reflection.
/// @param rotation The matrix to check.
/// @throws std::invalid_argument otherwise.
void RequireRotation(const std::array<double, 9>& rotation);

/// @brief The unit quaternion of a rotation.
///
/// Reference: S. W. Shepperd, "Quaternion from rotation matrix", Journal of Guidance and Control
/// 1(3):223-224, 1978. https://doi.org/10.2514/3.55767b
/// @param rotation A proper rotation, row-major 3 x 3.
/// @return The quaternion as (x, y, z, w).
std::array<double, 4> RotationQuaternion(const std::array<double, 9>& rotation);

/// `mesh` with every vertex moved by `transform`.
TriangleMesh Transformed(const TriangleMesh& mesh, const RigidTransform& transform);

/// `mesh` with every vertex scaled about the origin by `scale` per axis; triangles flip when an
/// odd number of the factors are negative, so the mesh stays outward-facing.
/// @throws std::invalid_argument for a zero or non-finite factor.
TriangleMesh Scaled(const TriangleMesh& mesh, const Vec3d& scale);

}  // namespace terraingen
