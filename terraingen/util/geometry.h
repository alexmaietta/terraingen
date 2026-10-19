/// @file
/// @brief Small vector types, vector math and axis-aligned boxes.
#pragma once

#include <cmath>
#include <limits>
#include <numbers>

namespace terraingen {

/// One degree in radians.
inline constexpr double kDegree = std::numbers::pi / 180.0;

/// One radian in degrees.
inline constexpr double kRadian = 180.0 / std::numbers::pi;

/// The slope in degrees of a surface with height gradient (`dx`, `dy`) in metres per metre; NaN
/// where either is NaN.
inline double GradientSlopeDegrees(double dx, double dy) {
    return std::atan(std::hypot(dx, dy)) * kRadian;
}

/// A 2D point or offset in double precision.
struct Vec2d {
    double x = 0.0;
    double y = 0.0;

    /// Componentwise equality.
    bool operator==(const Vec2d&) const = default;
};

/// Componentwise sum.
inline Vec2d operator+(const Vec2d& a, const Vec2d& b) {
    return {a.x + b.x, a.y + b.y};
}

/// Componentwise difference.
inline Vec2d operator-(const Vec2d& a, const Vec2d& b) {
    return {a.x - b.x, a.y - b.y};
}

/// `v` negated.
inline Vec2d operator-(const Vec2d& v) {
    return {-v.x, -v.y};
}

/// `v` scaled by `s`.
inline Vec2d operator*(const Vec2d& v, double s) {
    return {v.x * s, v.y * s};
}

/// `v` with each component divided by `s`.
inline Vec2d operator/(const Vec2d& v, double s) {
    return {v.x / s, v.y / s};
}

/// A 3D point or vector in double precision.
struct Vec3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    /// Componentwise equality.
    bool operator==(const Vec3d&) const = default;
};

/// Componentwise sum.
inline Vec3d operator+(const Vec3d& a, const Vec3d& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

/// Componentwise difference.
inline Vec3d operator-(const Vec3d& a, const Vec3d& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

/// `v` negated.
inline Vec3d operator-(const Vec3d& v) {
    return {-v.x, -v.y, -v.z};
}

/// `v` scaled by `s`.
inline Vec3d operator*(const Vec3d& v, double s) {
    return {v.x * s, v.y * s, v.z * s};
}

/// `v` with each component divided by `s`.
inline Vec3d operator/(const Vec3d& v, double s) {
    return {v.x / s, v.y / s, v.z / s};
}

/// Dot product.
inline double Dot(const Vec3d& a, const Vec3d& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/// Cross product.
inline Vec3d Cross(const Vec3d& a, const Vec3d& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

/// Euclidean length.
inline double Length(const Vec3d& v) {
    return std::sqrt(Dot(v, v));
}

/// `v` scaled to unit length; the zero vector stays zero.
inline Vec3d Normalized(const Vec3d& v) {
    const double length = Length(v);
    return length > 0.0 ? v * (1.0 / length) : v;
}

/// An axis-aligned box [`minX`, `maxX`) x [`minY`, `maxY`) in surface coordinates (metres
/// on a plane, the world coordinates a raster's `GeoTransform` maps pixels to).
struct Bounds2d {
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;

    /// Whether the half-open box holds the point (`x`, `y`).
    bool Contains(double x, double y) const {
        return x >= minX && x < maxX && y >= minY && y < maxY;
    }

    /// The box grown by `margin` (in the box's units) on every side.
    Bounds2d Expanded(double margin) const {
        return {minX - margin, minY - margin, maxX + margin, maxY + margin};
    }
};

/// The smallest box that contains a point: [`x`, next double above `x`) x [`y`, next double
/// above `y`).
inline Bounds2d PointBounds(double x, double y) {
    constexpr double kInf = std::numeric_limits<double>::infinity();
    return {x, y, std::nextafter(x, kInf), std::nextafter(y, kInf)};
}

}  // namespace terraingen
