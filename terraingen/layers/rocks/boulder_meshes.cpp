#include "terraingen/layers/rocks/boulder_meshes.h"

#include <array>

#include <cmath>
#include <limits>

namespace terraingen {

BoulderPlacement PlacementAtSite(const BoulderPlacement& placement, const Vec2d& rock,
                                 const Vec2d& site, const Surface& surface) {
    if (!surface.IsSphere()) {
        return placement;
    }
    Vec3d e[2];
    Vec3d n[2];
    Vec3d u[2];
    surface.Frame(rock, e[0], n[0], u[0]);
    surface.Frame(site, e[1], n[1], u[1]);
    std::array<double, 9> local{};
    for (int r = 0; r < 3; ++r) {
        const Vec3d& siteAxis = r == 0 ? e[1] : (r == 1 ? n[1] : u[1]);
        for (int c = 0; c < 3; ++c) {
            const Vec3d& rockAxis = c == 0 ? e[0] : (c == 1 ? n[0] : u[0]);
            local[static_cast<std::size_t>(3 * r + c)] =
                siteAxis.x * rockAxis.x + siteAxis.y * rockAxis.y + siteAxis.z * rockAxis.z;
        }
    }
    const Vec3d pr = surface.Position(rock);
    const Vec3d ps = surface.Position(site);
    const Vec3d d{pr.x - ps.x, pr.y - ps.y, pr.z - ps.z};
    RigidTransform toSite;
    toSite.rotation = local;
    toSite.translation = {e[1].x * d.x + e[1].y * d.y + e[1].z * d.z,
                          n[1].x * d.x + n[1].y * d.y + n[1].z * d.z,
                          u[1].x * d.x + u[1].y * d.y + u[1].z * d.z};
    BoulderPlacement moved = placement;
    moved.pose = toSite.After(placement.pose);
    return moved;
}

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
