#include "terraingen/util/surface.h"

#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace terraingen {

namespace {

/// The east and north unit vectors at longitude `lon` and latitude `lat` (radians).
void LocalAxes(double lon, double lat, Vec3d& east, Vec3d& north) {
    const double sinLon = std::sin(lon);
    const double cosLon = std::cos(lon);
    const double sinLat = std::sin(lat);
    east = {-sinLon, cosLon, 0.0};
    north = {-sinLat * cosLon, -sinLat * sinLon, std::cos(lat)};
}

}  // namespace

Vec3d UnitVector(double lonDeg, double latDeg) {
    const double lon = lonDeg * kDegree;
    const double lat = latDeg * kDegree;
    const double cosLat = std::cos(lat);
    return {cosLat * std::cos(lon), cosLat * std::sin(lon), std::sin(lat)};
}

Surface Surface::Sphere(double radius) {
    RequirePositive(radius, "A sphere's radius");
    Surface surface;
    surface.kind_ = Kind::Body;
    surface.radius_ = radius;
    surface.unitLength_ = radius * kDegree;
    surface.radii_ = {radius, radius, radius};
    return surface;
}

Surface Surface::Ellipsoid(double a, double b, double c) {
    RequirePositive(a, "An ellipsoid's radius a");
    RequirePositive(b, "An ellipsoid's radius b");
    RequirePositive(c, "An ellipsoid's radius c");
    if (a == b && b == c) {
        return Sphere(a);
    }
    Surface surface;
    surface.kind_ = Kind::Body;
    surface.radius_ = std::cbrt(a * b * c);
    surface.unitLength_ = surface.radius_ * kDegree;
    surface.radii_ = {a, b, c};
    return surface;
}

double Surface::RadiusAt(const Vec2d& point) const {
    if (kind_ == Kind::Plane) {
        return 0.0;
    }
    if (!IsEllipsoid()) {
        return radius_;
    }
    const Vec3d u = UnitVector(point.x, point.y);
    const double ex = u.x / radii_.x;
    const double ey = u.y / radii_.y;
    const double ez = u.z / radii_.z;
    return 1.0 / std::sqrt(ex * ex + ey * ey + ez * ez);
}

double Surface::AreaPerSteradian(const Vec2d& point) const {
    if (kind_ == Kind::Plane) {
        return 1.0;
    }
    if (!IsEllipsoid()) {
        return radius_ * radius_;
    }
    const Vec3d p = Position(point);
    const double r = Length(p);
    const double cosine = Dot(p, EllipsoidNormal(p)) / r;
    return r * r / cosine;
}

Vec3d Surface::EllipsoidNormal(const Vec3d& p) const {
    const Vec3d g{p.x / (radii_.x * radii_.x), p.y / (radii_.y * radii_.y),
                  p.z / (radii_.z * radii_.z)};
    const double length = Length(g);
    return {g.x / length, g.y / length, g.z / length};
}

Vec2d Surface::EllipsoidOffset(const Vec2d& centre, const Vec2d& point) const {
    const Vec3d c = Position(centre);
    const Vec3d p = Position(point);
    const LocalFrame frame = Frame(centre);
    const Vec3d d{p.x - c.x, p.y - c.y, p.z - c.z};
    return {Dot(d, frame.east), Dot(d, frame.north)};
}

Vec2d Surface::EllipsoidLocate(const Vec2d& centre, const Vec2d& offset) const {
    if (offset.x == 0.0 && offset.y == 0.0) {
        return centre;
    }
    const Vec3d c = Position(centre);
    const LocalFrame frame = Frame(centre);
    const Vec3d q{c.x + offset.x * frame.east.x + offset.y * frame.north.x,
                  c.y + offset.x * frame.east.y + offset.y * frame.north.y,
                  c.z + offset.x * frame.east.z + offset.y * frame.north.z};
    const Vec3d& n = frame.up;
    const Vec3d inverse{1.0 / (radii_.x * radii_.x), 1.0 / (radii_.y * radii_.y),
                        1.0 / (radii_.z * radii_.z)};
    const double a = n.x * n.x * inverse.x + n.y * n.y * inverse.y + n.z * n.z * inverse.z;
    const double b = q.x * n.x * inverse.x + q.y * n.y * inverse.y + q.z * n.z * inverse.z;
    const double cq = q.x * q.x * inverse.x + q.y * q.y * inverse.y + q.z * q.z * inverse.z;
    const double discriminant = b * b - a * (cq - 1.0);
    const double t = (b - std::sqrt(std::max(discriminant, 0.0))) / a;
    return PointOf({q.x - t * n.x, q.y - t * n.y, q.z - t * n.z});
}

Vec2d Surface::SphereOffset(const Vec2d& centre, const Vec2d& point) const {
    const Vec3d c = UnitVector(centre.x, centre.y);
    const Vec3d p = UnitVector(point.x, point.y);
    Vec3d east;
    Vec3d north;
    LocalAxes(centre.x * kDegree, centre.y * kDegree, east, north);
    const double angle = std::atan2(Length(Cross(c, p)), Dot(c, p));
    const double e = Dot(p, east);
    const double n = Dot(p, north);
    const double across = std::hypot(e, n);
    if (across == 0.0) {
        return {0.0, angle == 0.0 ? 0.0 : radius_ * angle};
    }
    const double scale = radius_ * angle / across;
    return {e * scale, n * scale};
}

Vec2d Surface::SphereLocate(const Vec2d& centre, const Vec2d& offset) const {
    const double distance = std::hypot(offset.x, offset.y);
    if (distance == 0.0) {
        return centre;
    }
    const Vec3d c = UnitVector(centre.x, centre.y);
    Vec3d east;
    Vec3d north;
    LocalAxes(centre.x * kDegree, centre.y * kDegree, east, north);
    const double angle = distance / radius_;
    const double along = std::sin(angle) / distance;
    const double cosAngle = std::cos(angle);
    const Vec3d p{cosAngle * c.x + along * (offset.x * east.x + offset.y * north.x),
                  cosAngle * c.y + along * (offset.x * east.y + offset.y * north.y),
                  cosAngle * c.z + along * (offset.x * east.z + offset.y * north.z)};
    return PointOf(p);
}

double Surface::Distance(const Vec2d& a, const Vec2d& b) const {
    if (kind_ == Kind::Plane) {
        return std::hypot(b.x - a.x, b.y - a.y);
    }
    if (IsEllipsoid()) {
        const Vec3d p = Position(a);
        const Vec3d q = Position(b);
        return Length(Vec3d{q.x - p.x, q.y - p.y, q.z - p.z});
    }
    const Vec3d u = UnitVector(a.x, a.y);
    const Vec3d v = UnitVector(b.x, b.y);
    return radius_ * std::atan2(Length(Cross(u, v)), Dot(u, v));
}

Vec3d Surface::Position(const Vec2d& point) const {
    if (kind_ == Kind::Plane) {
        return {point.x, point.y, 0.0};
    }
    const Vec3d unit = UnitVector(point.x, point.y);
    const double r = IsEllipsoid() ? RadiusAt(point) : radius_;
    return {r * unit.x, r * unit.y, r * unit.z};
}

LocalFrame Surface::Frame(const Vec2d& point) const {
    if (kind_ == Kind::Plane) {
        return {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    }
    LocalFrame frame;
    LocalAxes(point.x * kDegree, point.y * kDegree, frame.east, frame.north);
    frame.up = UnitVector(point.x, point.y);
    if (IsEllipsoid()) {
        const Vec3d n = EllipsoidNormal(Position(point));
        const double along = Dot(frame.east, n);
        const Vec3d east{frame.east.x - along * n.x, frame.east.y - along * n.y,
                         frame.east.z - along * n.z};
        const double length = Length(east);
        frame.up = n;
        frame.east = {east.x / length, east.y / length, east.z / length};
        frame.north = Cross(n, frame.east);
    }
    return frame;
}

Vec2d Surface::PointOf(const Vec3d& direction) const {
    if (kind_ == Kind::Plane) {
        return {direction.x, direction.y};
    }
    const double lon = std::atan2(direction.y, direction.x) / kDegree;
    const double lat = std::atan2(direction.z, std::hypot(direction.x, direction.y)) / kDegree;
    return {lon == -180.0 ? 180.0 : lon, lat};
}

BucketIndex Surface::Bucket(const Vec2d& point, double size) const {
    const Vec3d p = Position(point);
    return {static_cast<long long>(std::floor(p.x / size)),
            static_cast<long long>(std::floor(p.y / size)),
            static_cast<long long>(std::floor(p.z / size))};
}

Bounds2d Surface::Expand(const Bounds2d& region, double metres) const {
    if (kind_ == Kind::Plane) {
        return region.Expanded(metres);
    }
    double dLat = metres / unitLength_;
    double angle = metres / radius_;
    if (IsEllipsoid()) {
        const double shortest = std::min({radii_.x, radii_.y, radii_.z});
        angle = metres < shortest ? std::asin(metres / shortest) : std::numbers::pi;
        dLat = angle / kDegree;
    }
    Bounds2d expanded{region.minX, std::max(-90.0, region.minY - dLat), region.maxX,
                      std::min(90.0, region.maxY + dLat)};
    const double widest = std::max(std::fabs(region.minY), std::fabs(region.maxY));
    const double reach = std::sin(std::min(angle, std::numbers::pi / 2.0));
    const double cosLat = std::cos(std::min(widest, 90.0) * kDegree);
    const double dLon = angle < std::numbers::pi / 2.0 && reach < cosLat
                            ? std::asin(reach / cosLat) / kDegree
                            : 360.0;
    if (expanded.minY <= -90.0 || expanded.maxY >= 90.0 || dLon >= 180.0 ||
        region.maxX - region.minX + 2.0 * dLon >= 360.0) {
        expanded.minX = -180.0;
        expanded.maxX = 180.0;
    } else {
        expanded.minX = region.minX - dLon;
        expanded.maxX = region.maxX + dLon;
    }
    return expanded;
}

}  // namespace terraingen
