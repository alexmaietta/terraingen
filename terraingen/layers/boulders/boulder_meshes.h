/// @file
/// @brief Boulders as 3D models: placing unit shapes on the ground, and their meshes.
#pragma once

#include "terraingen/layers/boulders/boulder.h"
#include "terraingen/meshes/boulder.h"
#include "terraingen/meshes/mesh.h"
#include "terraingen/util/surface.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace terraingen {

/// @brief How a unit `BoulderShape` becomes a boulder in the world: scaled per axis by `scale`,
/// then moved by `pose`.
struct BoulderPlacement {
    Vec3d scale{1.0, 1.0, 1.0};  ///< Scale along the shape's x, y and z, in metres.
    RigidTransform pose{};       ///< Rotation and translation after scaling.

    /// @brief Where a point of the unit shape lands.
    /// @param p The point, in shape coordinates.
    /// @return The point in the world.
    Vec3d Apply(const Vec3d& p) const {
        return pose.Apply({p.x * scale.x, p.y * scale.y, p.z * scale.z});
    }
};

/// @brief A boulder placed as a 3D model of its own.
struct BoulderModel {
    Boulder boulder;                            ///< Its record.
    std::uint64_t shapeSeed = 0;                ///< The seed of its shape.
    std::shared_ptr<const BoulderShape> shape;  ///< Its unit shape.
    BoulderPlacement placement;                 ///< The shape moved onto the ground beneath it.
};

/// @brief The height of the top of a placed mesh above a point.
/// @param placed The mesh, in world coordinates.
/// @param x World x of the point.
/// @param y World y of the point.
/// @return The highest of its upward-facing triangles there, or nothing outside its footprint.
std::optional<double> TopSurface(const TriangleMesh& placed, double x, double y);

/// @brief A boulder's placement moved from its own local frame into the local frame of a site.
///
/// On a sphere the site frame is east, north and up at `site` on the datum sphere, keeping a scene
/// about a site in metre-scale coordinates; on a plane `placement` is returned unchanged.
/// @param placement The placement, in the boulder's local frame (east, north, up above datum).
/// @param boulder Surface coordinates of the boulder.
/// @param site Surface coordinates of the site.
/// @param surface What the boulders lie on.
/// @return The placement in the site's frame.
BoulderPlacement PlacementAtSite(const BoulderPlacement& placement, const Vec2d& boulder,
                                 const Vec2d& site, const Surface& surface);

/// @brief The placement of a shape as a boulder standing on the ground.
///
/// The long axis is scaled to the diameter (the intermediate in proportion) and the short axis to
/// the height; then leaned by `lean` about the long axis, turned by `yaw`, and set at the
/// boulder's position with `burial` of its vertical extent below `groundHeight`.
/// @param boulder The boulder.
/// @param shape Its unit shape.
/// @param groundHeight Height of the ground under its centre, in metres.
/// @return The placement.
BoulderPlacement PlaceBoulderModel(const Boulder& boulder, const BoulderShape& shape,
                                   double groundHeight);

/// @brief A boulder as a mesh in the world, its own shape placed by `PlaceBoulderModel`.
/// @param boulder The boulder.
/// @param options The shape styles and detail.
/// @param groundHeight Height of the ground under its centre, in metres.
/// @param prototypes Number of shared shapes, or 0 for a shape per boulder (`BoulderShapeSeed`).
/// @return The mesh, in world coordinates.
/// @throws std::invalid_argument for invalid options.
TriangleMesh BoulderMesh(const Boulder& boulder, const BoulderShapeOptions& options,
                         double groundHeight, std::uint32_t prototypes = 0);

}  // namespace terraingen
