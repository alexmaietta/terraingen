#pragma once

#include <cmath>
#include <limits>
#include <numbers>

namespace terraingen {

/// One degree in radians.
inline constexpr double kDegree = std::numbers::pi / 180.0;

/// A 2D point or offset in double precision.
struct Vec2d {
    double x = 0.0;
    double y = 0.0;

    bool operator==(const Vec2d&) const = default;
};

/// A 3D point or vector in double precision.
struct Vec3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    bool operator==(const Vec3d&) const = default;
};

inline Vec3d operator+(const Vec3d& a, const Vec3d& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3d operator-(const Vec3d& a, const Vec3d& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3d operator*(const Vec3d& v, double s) {
    return {v.x * s, v.y * s, v.z * s};
}

inline double Dot(const Vec3d& a, const Vec3d& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3d Cross(const Vec3d& a, const Vec3d& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline double Length(const Vec3d& v) {
    return std::sqrt(Dot(v, v));
}

/// `v` scaled to unit length; the zero vector stays zero.
inline Vec3d Normalized(const Vec3d& v) {
    const double length = Length(v);
    return length > 0.0 ? v * (1.0 / length) : v;
}

/// An axis-aligned box [`minX`, `maxX`) x [`minY`, `maxY`) in surface coordinates: metres in the
/// projected frame of a planar surface, the same world coordinates a raster's `GeoTransform` maps
/// pixels to.
struct Bounds2d {
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;

    bool Contains(double x, double y) const {
        return x >= minX && x < maxX && y >= minY && y < maxY;
    }

    /// The box grown by `margin` on every side.
    Bounds2d Expanded(double margin) const {
        return {minX - margin, minY - margin, maxX + margin, maxY + margin};
    }
};

/// The smallest box that contains the point (`x`, `y`).
inline Bounds2d PointBounds(double x, double y) {
    constexpr double kInf = std::numeric_limits<double>::infinity();
    return {x, y, std::nextafter(x, kInf), std::nextafter(y, kInf)};
}

}  // namespace terraingen
