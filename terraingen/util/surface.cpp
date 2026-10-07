#include "terraingen/util/surface.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace terraingen {

namespace {

constexpr double kDegree = std::numbers::pi / 180.0;

double Dot(const Vec3d& a, const Vec3d& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3d Cross(const Vec3d& a, const Vec3d& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double Length(const Vec3d& v) {
    return std::sqrt(Dot(v, v));
}

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
    if (!(std::isfinite(radius) && radius > 0.0)) {
        throw std::invalid_argument("A sphere's radius must be positive and finite");
    }
    Surface surface;
    surface.kind_ = Kind::Sphere;
    surface.radius_ = radius;
    surface.unitLength_ = radius * kDegree;
    return surface;
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
    const Vec3d u = UnitVector(a.x, a.y);
    const Vec3d v = UnitVector(b.x, b.y);
    return radius_ * std::atan2(Length(Cross(u, v)), Dot(u, v));
}

Vec3d Surface::Position(const Vec2d& point) const {
    if (kind_ == Kind::Plane) {
        return {point.x, point.y, 0.0};
    }
    const Vec3d unit = UnitVector(point.x, point.y);
    return {radius_ * unit.x, radius_ * unit.y, radius_ * unit.z};
}

Vec2d Surface::PointOf(const Vec3d& direction) const {
    if (kind_ == Kind::Plane) {
        return {direction.x, direction.y};
    }
    const double lon = std::atan2(direction.y, direction.x) / kDegree;
    const double lat = std::atan2(direction.z, std::hypot(direction.x, direction.y)) / kDegree;
    return {lon == -180.0 ? 180.0 : lon, lat};
}

void Surface::Bucket(const Vec2d& point, double size, long long& i, long long& j,
                     long long& k) const {
    const Vec3d p = Position(point);
    i = static_cast<long long>(std::floor(p.x / size));
    j = static_cast<long long>(std::floor(p.y / size));
    k = static_cast<long long>(std::floor(p.z / size));
}

Bounds2d Surface::Expand(const Bounds2d& region, double metres) const {
    if (kind_ == Kind::Plane) {
        return region.Expanded(metres);
    }
    const double dLat = metres / unitLength_;
    Bounds2d expanded{region.minX, std::max(-90.0, region.minY - dLat), region.maxX,
                      std::min(90.0, region.maxY + dLat)};
    const double widest = std::max(std::fabs(region.minY), std::fabs(region.maxY));
    const double angle = metres / radius_;
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
