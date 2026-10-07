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

std::optional<double> TopSurface(const TriangleMesh& placed, double x, double y) {
    std::optional<double> top;
    for (const auto& t : placed.triangles) {
        const Vec3d& a = placed.positions[t[0]];
        const Vec3d& b = placed.positions[t[1]];
        const Vec3d& c = placed.positions[t[2]];
        const double area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
        if (!(area > 0.0)) {
            continue;
        }
        const double u = ((b.x - x) * (c.y - y) - (c.x - x) * (b.y - y)) / area;
        const double v = ((c.x - x) * (a.y - y) - (a.x - x) * (c.y - y)) / area;
        const double w = 1.0 - u - v;
        if (u < 0.0 || v < 0.0 || w < 0.0) {
            continue;
        }
        const double z = u * a.z + v * b.z + w * c.z;
        if (!top || z > *top) {
            top = z;
        }
    }
    return top;
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
