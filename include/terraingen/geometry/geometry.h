// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Small vector types, vector math and axis-aligned boxes.
#pragma once

#include "terraingen/util/bits.h"

#include <cmath>
#include <limits>

namespace terraingen {

/// One degree in radians.
inline constexpr double kDegree = kPi / 180.0;

/// One radian in degrees.
inline constexpr double kRadian = 180.0 / kPi;

/// The slope in degrees of a surface with height gradient (`dx`, `dy`) in meters per meter; NaN
/// where either is NaN.
inline double GradientSlopeDegrees(double dx, double dy) {
    return std::atan(std::hypot(dx, dy)) * kRadian;
}

/// A 2D point or offset in double precision.
struct Vec2d {
    double x = 0.0;
    double y = 0.0;

    /// Componentwise equality.
    bool operator==(const Vec2d& other) const {
        return x == other.x && y == other.y;
    }
    bool operator!=(const Vec2d& other) const {
        return !(*this == other);
    }
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
    bool operator==(const Vec3d& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
    bool operator!=(const Vec3d& other) const {
        return !(*this == other);
    }
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

/// An axis-aligned box [`minX`, `maxX`) x [`minY`, `maxY`) in surface coordinates (meters
/// on a plane, the world coordinates a raster's `GeoTransform` maps pixels to).
struct Bounds2d {
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;

    /// Whether every bound is finite.
    bool IsFinite() const {
        return std::isfinite(minX) && std::isfinite(minY) && std::isfinite(maxX) &&
               std::isfinite(maxY);
    }

    /// The center of the box.
    Vec2d Center() const {
        return {0.5 * (minX + maxX), 0.5 * (minY + maxY)};
    }

    /// Whether the half-open box holds the point (`x`, `y`).
    bool Contains(double x, double y) const {
        return x >= minX && x < maxX && y >= minY && y < maxY;
    }

    /// The box grown by `margin` (in the box's units) on every side.
    Bounds2d Expanded(double margin) const {
        return {minX - margin, minY - margin, maxX + margin, maxY + margin};
    }

    /// Memberwise equality.
    bool operator==(const Bounds2d& other) const {
        return minX == other.minX && minY == other.minY && maxX == other.maxX && maxY == other.maxY;
    }
    bool operator!=(const Bounds2d& other) const {
        return !(*this == other);
    }
};

/// The smallest box that contains a point: [`x`, next double above `x`) x [`y`, next double
/// above `y`).
inline Bounds2d PointBounds(double x, double y) {
    constexpr double kInf = std::numeric_limits<double>::infinity();
    return {x, y, std::nextafter(x, kInf), std::nextafter(y, kInf)};
}

}  // namespace terraingen
