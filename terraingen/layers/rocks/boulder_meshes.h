#pragma once

#include "terraingen/layers/rocks/rock.h"
#include "terraingen/meshes/boulder.h"
#include "terraingen/meshes/mesh.h"
#include "terraingen/util/surface.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace terraingen {

/// How a unit `BoulderShape` becomes a rock in the world: scaled per axis by `scale`, then moved by
/// `pose`. An instanced renderer draws the shape with this transform.
struct BoulderPlacement {
    Vec3d scale{1.0, 1.0, 1.0};
    RigidTransform pose{};

    /// Where the unit shape's point `p` lands.
    Vec3d Apply(const Vec3d& p) const {
        return pose.Apply({p.x * scale.x, p.y * scale.y, p.z * scale.z});
    }
};

/// A rock placed as a 3D model of its own: its record, its shape, the seed of that shape, and how
/// the unit shape is scaled and moved onto the ground beneath it.
struct BoulderModel {
    Rock rock;
    std::uint64_t shapeSeed = 0;
    std::shared_ptr<const BoulderShape> shape;
    BoulderPlacement placement;
};

/// The height of the top of `placed`, a mesh in the world, above (`x`, `y`): the highest of its
/// upward-facing triangles there, or nothing outside its footprint.
std::optional<double> TopSurface(const TriangleMesh& placed, double x, double y);

/// `placement` of a boulder at `rock` (surface coordinates), given in the rock's local frame (east,
/// north and up, with up the height above the datum), moved into the local frame of `site`: on a
/// sphere, the east, north and up axes at `site` on the datum sphere, so a scene of boulders about
/// a site keeps metre-scale coordinates (glTF stores single precision); on a plane, where
/// placements are already in the shared frame, `placement` unchanged. Rocks placed by a `RockLayer`
/// on a sphere have their placements in their local frames.
BoulderPlacement PlacementAtSite(const BoulderPlacement& placement, const Vec2d& rock,
                                 const Vec2d& site, const Surface& surface);

/// The placement of `shape` as `rock` on ground at `groundHeight` under its centre: the long axis
/// scaled to the rock's diameter, the intermediate axis in proportion, and the short axis to the
/// rock's height; then leaned by `rock.lean` about its long axis and turned by `rock.yaw` about the
/// vertical; then set at (`rock.x`, `rock.y`) with `rock.burial` of its vertical extent below
/// `groundHeight`.
BoulderPlacement PlaceBoulder(const Rock& rock, const BoulderShape& shape, double groundHeight);

/// `rock` as a mesh in the world, standing on ground at `groundHeight`: its own shape
/// (`BoulderShapeSeed(rock.id, prototypes)`) under `options`, placed by `PlaceBoulder`.
/// @throws std::invalid_argument for invalid options.
TriangleMesh BoulderMesh(const Rock& rock, const BoulderShapeOptions& options, double groundHeight,
                         std::uint32_t prototypes = 0);

}  // namespace terraingen
