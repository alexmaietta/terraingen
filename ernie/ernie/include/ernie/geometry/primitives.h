#pragma once

#include "ernie/geometry/mesh_data.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace ernie {

/// The axis a primitive is built around, as UsdGeom's `axis` token.
enum class Axis { X, Y, Z };

/// The axis named "X", "Y" or "Z"; none for anything else.
std::optional<Axis> AxisNamed(std::string_view name);

// Triangle meshes of UsdGeom's implicit primitives, centred on the origin, with outward per-vertex
// normals and counter-clockwise triangles seen from outside. Curved surfaces share vertices for
// smooth normals; faces meeting at an edge (box faces, caps, a cone's base) have their own.
//
// Shapes are built around +z and turned to `axis` by a cyclic permutation of the coordinates
// (z to x for X, z to y for Y), which keeps their handedness. `slices` divide circles and
// `stacks` divide a sphere from pole to pole (a capsule's caps get half each).
//
// Reference: Pixar, "UsdGeom: USD Geometry Schema",
// https://openusd.org/release/api/usd_geom_page_front.html (Cube, Sphere, Cylinder, Cone,
// Capsule and Plane).
// Each throws std::invalid_argument for sizes that are not positive and finite (the capsule's
// height, its cylinder's length, may be 0), or fewer than 3 slices or 2 stacks.

/// A box of edge lengths `size` (UsdGeomCube is a box of equal edges, default 2).
MeshData BoxMesh(const glm::vec3& size);

/// A UV sphere (UsdGeomSphere, default radius 1).
MeshData SphereMesh(float radius, std::uint32_t slices = 48, std::uint32_t stacks = 24);

/// A capped cylinder of total `height` along `axis` (UsdGeomCylinder, default radius 1, height 2).
MeshData CylinderMesh(float radius, float height, Axis axis = Axis::Z, std::uint32_t slices = 48);

/// A capped cone of total `height` along `axis` with its apex at +height / 2 (UsdGeomCone,
/// default radius 1, height 2).
MeshData ConeMesh(float radius, float height, Axis axis = Axis::Z, std::uint32_t slices = 48);

/// A cylinder of `height` with hemispherical caps of `radius` beyond it, so its total length is
/// height + 2 radius (UsdGeomCapsule, default radius 0.5, height 1).
MeshData CapsuleMesh(float radius, float height, Axis axis = Axis::Z, std::uint32_t slices = 48,
                     std::uint32_t stacks = 24);

/// A one-sided rectangle facing +`axis` (UsdGeomPlane, default 2 x 2): `width` along x and
/// `length` along y for Z, width along x and length along z for Y, width along z and length
/// along y for X.
MeshData PlaneMesh(float width, float length, Axis axis = Axis::Z);

}  // namespace ernie
