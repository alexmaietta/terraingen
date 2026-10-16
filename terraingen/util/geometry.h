/// @file
/// @brief Small vector types, vector math and axis-aligned boxes.
#pragma once

#include <cmath>
#include <limits>
#include <numbers>

namespace terraingen {

/// @brief One degree in radians.
inline constexpr double kDegree = std::numbers::pi / 180.0;

/// @brief A 2D point or offset in double precision.
struct Vec2d {
    double x = 0.0;
    double y = 0.0;

    /// @brief Componentwise equality.
    bool operator==(const Vec2d&) const = default;
};

/// @brief A 3D point or vector in double precision.
struct Vec3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    /// @brief Componentwise equality.
    bool operator==(const Vec3d&) const = default;
};

/// @brief Componentwise sum.
/// @param a First vector.
/// @param b Second vector.
/// @return `a` + `b`.
inline Vec3d operator+(const Vec3d& a, const Vec3d& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

/// @brief Componentwise difference.
/// @param a First vector.
/// @param b Second vector.
/// @return `a` - `b`.
inline Vec3d operator-(const Vec3d& a, const Vec3d& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

/// @brief Scales a vector.
/// @param v The vector.
/// @param s The scale factor.
/// @return `v` times `s`.
inline Vec3d operator*(const Vec3d& v, double s) {
    return {v.x * s, v.y * s, v.z * s};
}

/// @brief Dot product.
/// @param a First vector.
/// @param b Second vector.
/// @return `a` . `b`.
inline double Dot(const Vec3d& a, const Vec3d& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/// @brief Cross product.
/// @param a First vector.
/// @param b Second vector.
/// @return `a` x `b`.
inline Vec3d Cross(const Vec3d& a, const Vec3d& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

/// @brief Euclidean length.
/// @param v The vector.
/// @return |`v`|.
inline double Length(const Vec3d& v) {
    return std::sqrt(Dot(v, v));
}

/// @brief A vector scaled to unit length.
/// @param v The vector.
/// @return `v` / |`v`|; the zero vector stays zero.
inline Vec3d Normalized(const Vec3d& v) {
    const double length = Length(v);
    return length > 0.0 ? v * (1.0 / length) : v;
}

/// @brief An axis-aligned box [`minX`, `maxX`) x [`minY`, `maxY`) in surface coordinates (metres
/// on a plane, the world coordinates a raster's `GeoTransform` maps pixels to).
struct Bounds2d {
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;

    /// @brief Whether the half-open box holds a point.
    /// @param x The point's x.
    /// @param y The point's y.
    /// @return True for `minX` <= `x` < `maxX` and `minY` <= `y` < `maxY`.
    bool Contains(double x, double y) const {
        return x >= minX && x < maxX && y >= minY && y < maxY;
    }

    /// @brief The box grown on every side.
    /// @param margin The growth on each side, in the box's units.
    /// @return The expanded box.
    Bounds2d Expanded(double margin) const {
        return {minX - margin, minY - margin, maxX + margin, maxY + margin};
    }
};

/// @brief The smallest box that contains a point.
/// @param x The point's x.
/// @param y The point's y.
/// @return [`x`, next double above `x`) x [`y`, next double above `y`).
inline Bounds2d PointBounds(double x, double y) {
    constexpr double kInf = std::numeric_limits<double>::infinity();
    return {x, y, std::nextafter(x, kInf), std::nextafter(y, kInf)};
}

}  // namespace terraingen
