#include "terraingen/layers/rocks/boulder_meshes.h"

#include <cmath>
#include <limits>

namespace terraingen {

BoulderPlacement PlaceBoulder(const Rock& rock, const BoulderShape& shape, double groundHeight) {
    BoulderPlacement placement;
    placement.scale = {rock.diameter, rock.diameter, rock.height / shape.shortAxis};
    placement.pose =
        RigidTransform::RotationZ(rock.yaw).After(RigidTransform::RotationX(rock.lean));
    double low = std::numeric_limits<double>::infinity();
    double high = -low;
    for (const Vec3d& p : shape.mesh.positions) {
        const double z = placement.Apply(p).z;
        low = std::fmin(low, z);
        high = std::fmax(high, z);
    }
    placement.pose.translation = {rock.x, rock.y, groundHeight - rock.burial * (high - low) - low};
    return placement;
}

TriangleMesh BoulderMesh(const Rock& rock, const BoulderShapeOptions& options, double groundHeight,
                         std::uint32_t prototypes) {
    const BoulderShape shape = GenerateBoulderShape(BoulderShapeSeed(rock.id, prototypes), options);
    const BoulderPlacement placement = PlaceBoulder(rock, shape, groundHeight);
    TriangleMesh mesh = shape.mesh;
    for (Vec3d& p : mesh.positions) {
        p = placement.Apply(p);
    }
    return mesh;
}

}  // namespace terraingen
