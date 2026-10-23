/// @file
/// @brief Boulders as 3D models: placing unit shapes on the ground, and their meshes.
#pragma once

#include "terraingen/layers/boulders/boulder.h"
#include "terraingen/layers/boulders/boulder_shape.h"
#include "terraingen/mesh/mesh.h"
#include "terraingen/util/surface.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace terraingen {

/// How a unit `BoulderShape` becomes a boulder in the world: scaled per axis by `scale`,
/// then moved by `pose`.
struct BoulderPlacement {
    Vec3d scale{1.0, 1.0, 1.0};  ///< Scale along the shape's x, y and z, in meters.
    RigidTransform pose{};       ///< Rotation and translation after scaling.

    /// Where point `p` of the unit shape lands in the world.
    Vec3d Apply(const Vec3d& p) const {
        return pose.Apply({p.x * scale.x, p.y * scale.y, p.z * scale.z});
    }

    /// The point of the unit shape that lands at world point `p`.
    Vec3d Unapply(const Vec3d& p) const {
        const std::array<double, 9>& r = pose.rotation;
        const Vec3d d = p - pose.translation;
        return {(r[0] * d.x + r[3] * d.y + r[6] * d.z) / scale.x,
                (r[1] * d.x + r[4] * d.y + r[7] * d.z) / scale.y,
                (r[2] * d.x + r[5] * d.y + r[8] * d.z) / scale.z};
    }
};

/// `mesh`, a unit shape's mesh, with every point moved by `placement`.
TriangleMesh PlaceMesh(TriangleMesh mesh, const BoulderPlacement& placement);

/// The lowest and highest z of `points` once mapped by `map`.
template <typename Map>
std::pair<double, double> ZRange(const std::vector<Vec3d>& points, const Map& map) {
    double low = std::numeric_limits<double>::infinity();
    double high = -low;
    for (const Vec3d& p : points) {
        const double z = map(p).z;
        low = std::fmin(low, z);
        high = std::fmax(high, z);
    }
    return {low, high};
}

/// A boulder placed as a 3D model of its own.
struct BoulderModel {
    Boulder boulder;              ///< Its record.
    std::uint64_t shapeSeed = 0;  ///< The seed of its shape, or of its parent's for a fragment.
    std::shared_ptr<const BoulderShape> shape;  ///< Its unit shape.
    BoulderPlacement placement;                 ///< The shape moved onto the ground beneath it.
};

/// The height at world (x, y) of the highest upward-facing triangle of `placed` (a mesh in world
/// coordinates), or nothing outside its footprint.
std::optional<double> TopSurface(const TriangleMesh& placed, double x, double y);

/// A boulder's placement moved from its own local frame into the local frame of a site.
///
/// On a sphere the site frame is east, north and up at `site` on the datum sphere, keeping a scene
/// about a site in meter-scale coordinates; on a plane `placement` is returned unchanged.
/// @param placement In the boulder's local frame (east, north, up above datum).
/// @param boulder Surface coordinates of the boulder.
/// @param site Surface coordinates of the site.
BoulderPlacement PlacementAtSite(const BoulderPlacement& placement, const Vec2d& boulder,
                                 const Vec2d& site, const Surface& surface);

/// The rotation tipping the vertical by `tilt` radians toward `azimuth` (radians counter-clockwise
/// from east), about the horizontal axis square to that direction.
RigidTransform TiltRotation(double tilt, double azimuth);

/// The placement of a shape as a boulder standing on the ground.
///
/// The long axis is scaled to the diameter (the intermediate in proportion) and the short axis to
/// the height; then stood on the axis `rest` names, leaned by `lean` about the long axis, turned by
/// `yaw`, tipped by `tilt` toward `tiltAzimuth`, and set at the boulder's position with `burial`
/// of its vertical extent below `groundHeight`. With `settle`, before the tilt it is tipped over
/// on level ground, from that pose, until it rests stably with its center of mass above the face
/// or edges it stands on, so no shape is left balanced on a corner.
/// @param groundHeight Height of the ground under its center, in meters.
BoulderPlacement PlaceBoulderModel(const Boulder& boulder, const BoulderShape& shape,
                                   double groundHeight, bool settle = false);

/// The seed of `boulder`'s shape, or of its parent's whole shape for a fragment: its `shapeSeed`
/// when set, else `BoulderShapeSeed` of its ID, or its parent's.
/// @param prototypes Number of shared shapes, or 0 for a shape per boulder.
std::uint64_t ShapeSeed(const Boulder& boulder, std::uint32_t prototypes = 0);

}  // namespace terraingen
