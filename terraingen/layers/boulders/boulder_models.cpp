#include "terraingen/layers/boulders/boulder_models.h"

#include "terraingen/util/bits.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

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
        return RigidTransform::RotationX(0.5 * kPi);
    case BoulderRest::End:
        return RigidTransform::RotationY(-0.5 * kPi);
    case BoulderRest::Face:
        break;
    }
    return {};
}

/// The centre of mass of a closed mesh of uniform density, from its signed tetrahedra.
Vec3d MassCentre(const std::vector<Vec3d>& points,
                 const std::vector<std::array<std::uint32_t, 3>>& triangles) {
    Vec3d sum{};
    double volume = 0.0;
    for (const auto& t : triangles) {
        const Vec3d& a = points[t[0]];
        const Vec3d& b = points[t[1]];
        const Vec3d& c = points[t[2]];
        const double v = Dot(a, Cross(b, c));
        sum = sum + (a + b + c) * v;
        volume += v;
    }
    return sum * (1.0 / (4.0 * volume));
}

/// The convex hull of points in the plane, counter-clockwise, without collinear points.
std::vector<Vec2d> Hull2d(std::vector<Vec2d> points) {
    std::sort(points.begin(), points.end(), [](const Vec2d& a, const Vec2d& b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    if (points.size() < 3) {
        return points;
    }
    const auto turn = [](const Vec2d& o, const Vec2d& a, const Vec2d& b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };
    std::vector<Vec2d> hull(2 * points.size());
    std::size_t k = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        while (k >= 2 && turn(hull[k - 2], hull[k - 1], points[i]) <= 0.0) {
            --k;
        }
        hull[k++] = points[i];
    }
    for (std::size_t i = points.size() - 1, lower = k + 1; i-- > 0;) {
        while (k >= lower && turn(hull[k - 2], hull[k - 1], points[i]) <= 0.0) {
            --k;
        }
        hull[k++] = points[i];
    }
    hull.resize(k - 1);
    return hull;
}

/// The point of `hull`'s boundary nearest `p`, and whether `p` lies inside a hull of 3 or more
/// points.
std::pair<Vec2d, bool> NearestOnHull(const std::vector<Vec2d>& hull, const Vec2d& p) {
    if (hull.size() == 1) {
        return {hull[0], false};
    }
    bool inside = hull.size() >= 3;
    Vec2d best = hull[0];
    double bestDistance = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < hull.size(); ++i) {
        const Vec2d& a = hull[i];
        const Vec2d& b = hull[(i + 1) % hull.size()];
        const double ex = b.x - a.x;
        const double ey = b.y - a.y;
        if (ex * (p.y - a.y) - ey * (p.x - a.x) < 0.0) {
            inside = false;
        }
        const double t =
            std::clamp(((p.x - a.x) * ex + (p.y - a.y) * ey) / (ex * ex + ey * ey), 0.0, 1.0);
        const Vec2d q{a.x + t * ex, a.y + t * ey};
        const double d = std::hypot(p.x - q.x, p.y - q.y);
        if (d < bestDistance) {
            bestDistance = d;
            best = q;
        }
    }
    return {best, inside};
}

/// The product `a` `b` of two row-major 3 x 3 matrices.
std::array<double, 9> Multiply(const std::array<double, 9>& a, const std::array<double, 9>& b) {
    std::array<double, 9> product{};
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t k = 0; k < 3; ++k) {
            for (std::size_t j = 0; j < 3; ++j) {
                product[3 * r + k] += a[3 * r + j] * b[3 * j + k];
            }
        }
    }
    return product;
}

/// One tip of a settling shape: a turn by `angle` radians about the horizontal axis `axis`
/// through `origin`, the side `toward` (a horizontal unit vector square to it) going down.
struct PivotStep {
    Vec3d origin;
    Vec3d axis;
    Vec3d toward;
    double cs = 1.0;
    double sn = 0.0;

    /// Where the step turns `p`.
    Vec3d Turn(const Vec3d& p) const {
        const Vec3d up{0.0, 0.0, 1.0};
        const Vec3d v = p - origin;
        const double u = Dot(v, toward);
        const double w = v.z;
        return origin + axis * Dot(v, axis) + toward * (u * cs + w * sn) + up * (w * cs - u * sn);
    }

    /// The step's rotation as a row-major matrix.
    std::array<double, 9> Rotation() const {
        std::array<double, 9> m{};
        const std::array<double, 3> av{axis.x, axis.y, axis.z};
        const std::array<double, 3> dv{toward.x, toward.y, toward.z};
        const std::array<double, 3> zv{0.0, 0.0, 1.0};
        for (std::size_t r = 0; r < 3; ++r) {
            for (std::size_t k = 0; k < 3; ++k) {
                m[3 * r + k] = av[r] * av[k] + cs * (dv[r] * dv[k] + zv[r] * zv[k]) +
                               sn * (dv[r] * zv[k] - zv[r] * dv[k]);
            }
        }
        return m;
    }
};

/// The next tip of `points` resting on the plane through their lowest point toward `centre`,
/// about the edge or corner of the contact nearest it, until another point touches; none once
/// the centre stands over the contact, within `tolerance`.
std::optional<PivotStep> NextPivot(const std::vector<Vec3d>& points, const Vec3d& centre,
                                   double tolerance) {
    const double low = ZRange(points, [](const Vec3d& p) { return p; }).first;
    std::vector<Vec2d> contact;
    for (const Vec3d& p : points) {
        if (p.z <= low + tolerance) {
            contact.push_back({p.x, p.y});
        }
    }
    const Vec2d c{centre.x, centre.y};
    const auto [pivot, inside] = NearestOnHull(Hull2d(contact), c);
    const double distance = std::hypot(c.x - pivot.x, c.y - pivot.y);
    if (inside || distance <= tolerance) {
        return std::nullopt;
    }
    PivotStep step;
    step.toward = {(c.x - pivot.x) / distance, (c.y - pivot.y) / distance, 0.0};
    step.axis = {-step.toward.y, step.toward.x, 0.0};
    step.origin = {pivot.x, pivot.y, low};
    double rise = 0.0;
    double run = 0.0;
    for (const Vec3d& p : points) {
        const double u = Dot(p - step.origin, step.toward);
        if (u > tolerance && (run == 0.0 || (p.z - low) * run < rise * u)) {
            rise = p.z - low;
            run = u;
        }
    }
    if (run == 0.0) {
        return std::nullopt;
    }
    const double angle = std::atan2(rise, run);
    step.cs = std::cos(angle);
    step.sn = std::sin(angle);
    return step;
}

/// The rotation that tips `points` over on a level plane until they rest: each step pivots about
/// the edge or corner of the ground contact nearest the centre of mass, toward it, until another
/// point touches, and stops once the centre of mass stands over the contact.
std::array<double, 9> SettleRotation(std::vector<Vec3d> points, Vec3d centre) {
    std::array<double, 9> total{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    double extent = 0.0;
    for (const Vec3d& p : points) {
        extent =
            std::fmax(extent, std::fmax(std::fabs(p.x), std::fmax(std::fabs(p.y), std::fabs(p.z))));
    }
    const double tolerance = 1e-7 * extent;
    for (int n = 0; n < 64; ++n) {
        const std::optional<PivotStep> step = NextPivot(points, centre, tolerance);
        if (!step) {
            break;
        }
        for (Vec3d& p : points) {
            p = step->Turn(p);
        }
        centre = step->Turn(centre);
        total = Multiply(step->Rotation(), total);
    }
    return total;
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
                                   double groundHeight, bool settle) {
    BoulderPlacement placement;
    placement.scale = {boulder.diameter, boulder.diameter, boulder.height / shape.shortAxis};
    placement.pose = RigidTransform::RotationZ(boulder.yaw)
                         .After(RigidTransform::RotationX(boulder.lean))
                         .After(RestRotation(boulder.rest));
    if (settle && !shape.mesh.triangles.empty()) {
        const std::vector<Vec3d> points = PlaceMesh(shape.mesh, placement).positions;
        RigidTransform rest;
        rest.rotation = SettleRotation(points, MassCentre(points, shape.mesh.triangles));
        placement.pose = rest.After(placement.pose);
    }
    if (boulder.tilt != 0.0) {
        placement.pose = TiltRotation(boulder.tilt, boulder.tiltAzimuth).After(placement.pose);
    }
    const auto [low, high] =
        ZRange(shape.mesh.positions, [&](const Vec3d& p) { return placement.Apply(p); });
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

std::uint64_t ShapeSeed(const Boulder& boulder, std::uint32_t prototypes) {
    if (boulder.shapeSeed) {
        return *boulder.shapeSeed;
    }
    return BoulderShapeSeed(boulder.parent != 0 ? boulder.parent : boulder.id, prototypes);
}

TriangleMesh BoulderMesh(const Boulder& boulder, const BoulderShapeOptions& options,
                         double groundHeight, std::uint32_t prototypes) {
    const BoulderShape shape = GenerateBoulderShape(
        boulder.shapeSeed ? *boulder.shapeSeed : BoulderShapeSeed(boulder.id, prototypes), options);
    return PlaceMesh(shape.mesh, PlaceBoulderModel(boulder, shape, groundHeight));
}

TriangleMesh PlaceMesh(TriangleMesh mesh, const BoulderPlacement& placement) {
    for (Vec3d& p : mesh.positions) {
        p = placement.Apply(p);
    }
    return mesh;
}

}  // namespace terraingen
