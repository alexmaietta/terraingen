#include "terraingen/util/surface.h"

#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>

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

/// The volumetric mean radius in metres of a shape model, from the mean of r^3 over directions.
double MeanRadius(const RadialShape& shape) {
    constexpr int kSteps = 180;
    double cubed = 0.0;
    double weight = 0.0;
    for (int j = 0; j < kSteps; ++j) {
        const double lat = -90.0 + (j + 0.5) * 180.0 / kSteps;
        const double cosLat = std::cos(lat * kDegree);
        for (int i = 0; i < 2 * kSteps; ++i) {
            const double r = shape.At(UnitVector(-180.0 + (i + 0.5) * 180.0 / kSteps, lat));
            cubed += r * r * r * cosLat;
            weight += cosLat;
        }
    }
    return std::cbrt(cubed / weight);
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
    surface.form_ = Form::Sphere;
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
    surface.form_ = Form::Ellipsoid;
    surface.radius_ = std::cbrt(a * b * c);
    surface.unitLength_ = surface.radius_ * kDegree;
    surface.radii_ = {a, b, c};
    return surface;
}

Surface Surface::ShapeModel(std::shared_ptr<const RadialShape> shape) {
    if (!shape) {
        throw std::invalid_argument("A shape model surface needs a radial shape");
    }
    Surface surface;
    surface.form_ = Form::Shape;
    surface.radius_ = MeanRadius(*shape);
    surface.unitLength_ = surface.radius_ * kDegree;
    surface.shape_ = std::move(shape);
    return surface;
}

double Surface::MinRadius() const {
    if (form_ == Form::Shape) {
        return shape_->MinRadius();
    }
    return std::min({radii_.x, radii_.y, radii_.z});
}

double Surface::MaxRadius() const {
    if (form_ == Form::Shape) {
        return shape_->MaxRadius();
    }
    return std::max({radii_.x, radii_.y, radii_.z});
}

double Surface::RadiusAt(const Vec2d& point) const {
    switch (form_) {
    case Form::Plane:
        return 0.0;
    case Form::Sphere:
        return radius_;
    case Form::Shape:
        return shape_->At(UnitVector(point.x, point.y));
    case Form::Ellipsoid:
        break;
    }
    const Vec3d u = UnitVector(point.x, point.y);
    const double ex = u.x / radii_.x;
    const double ey = u.y / radii_.y;
    const double ez = u.z / radii_.z;
    return 1.0 / std::sqrt(ex * ex + ey * ey + ez * ez);
}

double Surface::AreaPerSteradian(const Vec2d& point) const {
    switch (form_) {
    case Form::Plane:
        return 1.0;
    case Form::Sphere:
        return radius_ * radius_;
    case Form::Ellipsoid:
    case Form::Shape:
        break;
    }
    const Vec3d p = Position(point);
    const double r = Length(p);
    const double cosine = Dot(p, Normal(point, p)) / r;
    return r * r / cosine;
}

Vec3d Surface::Normal(const Vec2d& point, const Vec3d& p) const {
    Vec3d n;
    if (form_ == Form::Shape) {
        constexpr double kStep = 1e-6;
        const Vec3d u = UnitVector(point.x, point.y);
        Vec3d east;
        Vec3d north;
        LocalAxes(point.x * kDegree, point.y * kDegree, east, north);
        const auto at = [&](const Vec3d& axis, double sign) {
            const Vec3d d = u + axis * (sign * kStep);
            const Vec3d unit = d / Length(d);
            return unit * shape_->At(unit);
        };
        n = Cross(at(east, 1.0) - at(east, -1.0), at(north, 1.0) - at(north, -1.0));
    } else {
        n = {p.x / (radii_.x * radii_.x), p.y / (radii_.y * radii_.y), p.z / (radii_.z * radii_.z)};
    }
    return n / Length(n);
}

Vec2d Surface::TangentOffset(const Vec2d& centre, const Vec2d& point) const {
    const LocalFrame frame = Frame(centre);
    const Vec3d d = Position(point) - Position(centre);
    return {Dot(d, frame.east), Dot(d, frame.north)};
}

Vec2d Surface::ShapeLocate(const Vec2d& centre, const Vec2d& offset) const {
    if (offset.x == 0.0 && offset.y == 0.0) {
        return centre;
    }
    const Vec3d c = Position(centre);
    const LocalFrame frame = Frame(centre);
    const Vec3d q = c + frame.east * offset.x + frame.north * offset.y;
    const auto along = [&](double s) { return q - frame.up * s; };
    const auto excess = [&](double s) {
        const Vec3d x = along(s);
        const double r = Length(x);
        return r - shape_->At(x / r);
    };
    const double step = shape_->MaxRadius() / 64.0;
    double inside = 0.0;
    double outside = 0.0;
    if (excess(0.0) >= 0.0) {
        inside = step;
        while (excess(inside) >= 0.0 && inside < 4.0 * shape_->MaxRadius()) {
            outside = inside;
            inside += step;
        }
    } else {
        outside = -step;
        while (excess(outside) < 0.0 && outside > -4.0 * shape_->MaxRadius()) {
            inside = outside;
            outside -= step;
        }
    }
    for (int iteration = 0; iteration < 100; ++iteration) {
        const double middle = 0.5 * (inside + outside);
        (excess(middle) >= 0.0 ? outside : inside) = middle;
    }
    return Coordinates(along(0.5 * (inside + outside)));
}

Vec2d Surface::EllipsoidLocate(const Vec2d& centre, const Vec2d& offset) const {
    if (offset.x == 0.0 && offset.y == 0.0) {
        return centre;
    }
    const Vec3d c = Position(centre);
    const LocalFrame frame = Frame(centre);
    const Vec3d q = c + frame.east * offset.x + frame.north * offset.y;
    const Vec3d& n = frame.up;
    const Vec3d inverse{1.0 / (radii_.x * radii_.x), 1.0 / (radii_.y * radii_.y),
                        1.0 / (radii_.z * radii_.z)};
    const double a = n.x * n.x * inverse.x + n.y * n.y * inverse.y + n.z * n.z * inverse.z;
    const double b = q.x * n.x * inverse.x + q.y * n.y * inverse.y + q.z * n.z * inverse.z;
    const double cq = q.x * q.x * inverse.x + q.y * q.y * inverse.y + q.z * q.z * inverse.z;
    const double discriminant = b * b - a * (cq - 1.0);
    const double t = (b - std::sqrt(std::max(discriminant, 0.0))) / a;
    return Coordinates(q - n * t);
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
    return Coordinates(c * cosAngle + (east * offset.x + north * offset.y) * along);
}

double Surface::Distance(const Vec2d& a, const Vec2d& b) const {
    switch (form_) {
    case Form::Plane:
        return std::hypot(b.x - a.x, b.y - a.y);
    case Form::Ellipsoid:
    case Form::Shape:
        return Length(Position(b) - Position(a));
    case Form::Sphere:
        break;
    }
    const Vec3d u = UnitVector(a.x, a.y);
    const Vec3d v = UnitVector(b.x, b.y);
    return radius_ * std::atan2(Length(Cross(u, v)), Dot(u, v));
}

Vec3d Surface::Position(const Vec2d& point) const {
    if (form_ == Form::Plane) {
        return {point.x, point.y, 0.0};
    }
    const Vec3d unit = UnitVector(point.x, point.y);
    return unit * (form_ == Form::Sphere ? radius_ : RadiusAt(point));
}

LocalFrame Surface::Frame(const Vec2d& point) const {
    if (form_ == Form::Plane) {
        return {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    }
    LocalFrame frame;
    LocalAxes(point.x * kDegree, point.y * kDegree, frame.east, frame.north);
    frame.up = UnitVector(point.x, point.y);
    if (IsNonSphere()) {
        const Vec3d n = Normal(point, Position(point));
        const Vec3d east = frame.east - n * Dot(frame.east, n);
        frame.up = n;
        frame.east = east / Length(east);
        frame.north = Cross(n, frame.east);
    }
    return frame;
}

Vec2d Surface::Coordinates(const Vec3d& direction) const {
    if (form_ == Form::Plane) {
        return {direction.x, direction.y};
    }
    const double lon = std::atan2(direction.y, direction.x) * kRadian;
    const double lat = std::atan2(direction.z, std::hypot(direction.x, direction.y)) * kRadian;
    return {lon == -180.0 ? 180.0 : lon, lat};
}

BucketIndex Surface::Bucket(const Vec2d& point, double size) const {
    const Vec3d p = Position(point);
    return {static_cast<long long>(std::floor(p.x / size)),
            static_cast<long long>(std::floor(p.y / size)),
            static_cast<long long>(std::floor(p.z / size))};
}

Bounds2d Surface::Expand(const Bounds2d& region, double metres) const {
    if (form_ == Form::Plane) {
        return region.Expanded(metres);
    }
    double dLat = metres / unitLength_;
    double angle = metres / radius_;
    if (IsNonSphere()) {
        const double shortest = MinRadius();
        angle = metres < shortest ? std::asin(metres / shortest) : kPi;
        dLat = angle * kRadian;
    }
    Bounds2d expanded{region.minX, std::max(-90.0, region.minY - dLat), region.maxX,
                      std::min(90.0, region.maxY + dLat)};
    const double widest = std::max(std::fabs(region.minY), std::fabs(region.maxY));
    const double reach = std::sin(std::min(angle, kPi / 2.0));
    const double cosLat = std::cos(std::min(widest, 90.0) * kDegree);
    const double dLon =
        angle < kPi / 2.0 && reach < cosLat ? std::asin(reach / cosLat) * kRadian : 360.0;
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
