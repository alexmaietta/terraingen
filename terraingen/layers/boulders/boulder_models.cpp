#include "terraingen/layers/boulders/boulder_models.h"

#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace terraingen {

namespace {

/// Axis `index` of `frame`: 0 east, 1 north, 2 up.
const Vec3d& Axis(const LocalFrame& frame, int index) {
    return index == 0 ? frame.east : (index == 1 ? frame.north : frame.up);
}

/// The rotation turning the shape's axis that `rest` names to the vertical.
RigidTransform RestRotation(BoulderRest rest) {
    switch (rest) {
    case BoulderRest::Side:
        return RigidTransform::RotationX(0.5 * std::numbers::pi);
    case BoulderRest::End:
        return RigidTransform::RotationY(-0.5 * std::numbers::pi);
    case BoulderRest::Face:
        break;
    }
    return {};
}

}  // namespace

RigidTransform TiltRotation(double tilt, double azimuth) {
    const double ax = -std::sin(azimuth);
    const double ay = std::cos(azimuth);
    const double c = std::cos(tilt);
    const double s = std::sin(tilt);
    const double t = 1.0 - c;
    RigidTransform turn;
    turn.rotation = {c + t * ax * ax, t * ax * ay, s * ay, t * ax * ay, c + t * ay * ay, -s * ax,
                     -s * ay,         s * ax,      c};
    return turn;
}

BoulderPlacement PlacementAtSite(const BoulderPlacement& placement, const Vec2d& boulder,
                                 const Vec2d& site, const Surface& surface) {
    if (!surface.IsBody()) {
        return placement;
    }
    const LocalFrame from = surface.Frame(boulder);
    const LocalFrame to = surface.Frame(site);
    std::array<double, 9> local{};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            local[static_cast<std::size_t>(3 * r + c)] = Dot(Axis(to, r), Axis(from, c));
        }
    }
    const Vec3d pr = surface.Position(boulder);
    const Vec3d ps = surface.Position(site);
    const Vec3d d{pr.x - ps.x, pr.y - ps.y, pr.z - ps.z};
    RigidTransform toSite;
    toSite.rotation = local;
    toSite.translation = {Dot(to.east, d), Dot(to.north, d), Dot(to.up, d)};
    BoulderPlacement moved = placement;
    moved.pose = toSite.After(placement.pose);
    return moved;
}

BoulderPlacement PlaceBoulderModel(const Boulder& boulder, const BoulderShape& shape,
                                   double groundHeight) {
    BoulderPlacement placement;
    placement.scale = {boulder.diameter, boulder.diameter, boulder.height / shape.shortAxis};
    placement.pose = RigidTransform::RotationZ(boulder.yaw)
                         .After(RigidTransform::RotationX(boulder.lean))
                         .After(RestRotation(boulder.rest));
    if (boulder.tilt != 0.0) {
        placement.pose = TiltRotation(boulder.tilt, boulder.tiltAzimuth).After(placement.pose);
    }
    double low = std::numeric_limits<double>::infinity();
    double high = -low;
    for (const Vec3d& p : shape.mesh.positions) {
        const double z = placement.Apply(p).z;
        low = std::fmin(low, z);
        high = std::fmax(high, z);
    }
    placement.pose.translation = {boulder.x, boulder.y,
                                  groundHeight - boulder.burial * (high - low) - low};
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

TriangleMesh BoulderMesh(const Boulder& boulder, const BoulderShapeOptions& options,
                         double groundHeight, std::uint32_t prototypes) {
    const BoulderShape shape =
        GenerateBoulderShape(BoulderShapeSeed(boulder.id, prototypes), options);
    const BoulderPlacement placement = PlaceBoulderModel(boulder, shape, groundHeight);
    TriangleMesh mesh = shape.mesh;
    for (Vec3d& p : mesh.positions) {
        p = placement.Apply(p);
    }
    return mesh;
}

}  // namespace terraingen
