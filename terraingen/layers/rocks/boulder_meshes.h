#pragma once

#include "terraingen/layers/rocks/rock.h"
#include "terraingen/meshes/boulder.h"
#include "terraingen/meshes/mesh.h"

#include <cstdint>

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
