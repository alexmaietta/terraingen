#pragma once

namespace terraingen {

/// A 2D point or offset in double precision.
struct Vec2d {
    double x = 0.0;
    double y = 0.0;
};

/// A 3D point or vector in double precision.
struct Vec3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    bool operator==(const Vec3d&) const = default;
};

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

}  // namespace terraingen
