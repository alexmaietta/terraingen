/// @file
/// @brief `Surface`, the plane or sphere a terrain lies on, with local frames and distances.
#pragma once

#include "terraingen/util/geometry.h"

namespace terraingen {

/// @brief The east, north and up unit vectors at a surface point.
struct LocalFrame {
    Vec3d east;   ///< Unit vector east.
    Vec3d north;  ///< Unit vector north.
    Vec3d up;     ///< Unit vector up, away from a sphere's centre.
};

/// @brief The integer coordinates of a cube of a 3D grid.
struct BucketIndex {
    long long i = 0;
    long long j = 0;
    long long k = 0;

    /// @brief Componentwise equality.
    bool operator==(const BucketIndex&) const = default;
};

/// @brief The shape a terrain lies on, a plane or a sphere, measuring distances and directions
/// between surface coordinates.
///
/// On a plane, surface coordinates are metres in a projected frame. On a sphere of radius R they
/// are longitude and latitude in degrees (east and north positive), and the local frame of a point
/// is its azimuthal equidistant projection in metres.
class Surface {
  public:
    /// @brief The kind of surface.
    enum class Kind { Plane, Sphere };

    /// @brief A plane, the default.
    Surface() = default;

    /// @brief A plane.
    /// @return A plane surface.
    static Surface Plane() {
        return {};
    }

    /// @brief A sphere.
    /// @param radius The radius in metres.
    /// @return A sphere surface.
    /// @throws std::invalid_argument unless `radius` is positive and finite.
    static Surface Sphere(double radius);

    /// @brief The kind of surface.
    /// @return `Plane` or `Sphere`.
    Kind GetKind() const {
        return kind_;
    }

    /// @brief Whether the surface is a sphere.
    /// @return True for a sphere.
    bool IsSphere() const {
        return kind_ == Kind::Sphere;
    }

    /// @brief The sphere's radius.
    /// @return The radius in metres; 0 for a plane.
    double Radius() const {
        return radius_;
    }

    /// @brief Metres per surface unit along a meridian.
    /// @return 1 on a plane, R pi / 180 on a sphere.
    double UnitLength() const {
        return kind_ == Kind::Plane ? 1.0 : unitLength_;
    }

    /// @brief A point in the local east-north frame of a centre.
    /// @param centre The frame's origin, in surface coordinates.
    /// @param point The point, in surface coordinates.
    /// @return East and north offsets in metres; exactly `point` - `centre` on a plane.
    Vec2d Offset(const Vec2d& centre, const Vec2d& point) const {
        if (kind_ == Kind::Plane) {
            return {point.x - centre.x, point.y - centre.y};
        }
        return SphereOffset(centre, point);
    }

    /// @brief The point at an offset in a centre's local frame: the inverse of `Offset`.
    /// @param centre The frame's origin, in surface coordinates.
    /// @param offset East and north offsets in metres.
    /// @return The point in surface coordinates; on a sphere the longitude is in (-180, 180].
    Vec2d Locate(const Vec2d& centre, const Vec2d& offset) const {
        if (kind_ == Kind::Plane) {
            return {centre.x + offset.x, centre.y + offset.y};
        }
        return SphereLocate(centre, offset);
    }

    /// @brief The distance between two points: straight on a plane, along a great circle on a
    /// sphere.
    /// @param a First point, in surface coordinates.
    /// @param b Second point, in surface coordinates.
    /// @return The distance in metres.
    double Distance(const Vec2d& a, const Vec2d& b) const;

    /// @brief A point in 3D.
    /// @param point The point, in surface coordinates.
    /// @return (x, y, 0) on a plane; on a sphere R times the unit vector of `UnitVector`, in
    /// metres.
    Vec3d Position(const Vec2d& point) const;

    /// @brief The east, north and up unit vectors at a point in the frame of `Position`: the x, y
    /// and z axes on a plane; the local frame of `Offset` on a sphere.
    /// @param point The surface point.
    /// @return The three axes.
    LocalFrame Frame(const Vec2d& point) const;

    /// @brief The surface point of a direction from the sphere's centre.
    /// @param direction The direction, of any length but 0.
    /// @return The point in surface coordinates; (x, y) on a plane.
    Vec2d PointOf(const Vec3d& direction) const;

    /// @brief A box containing every point within a distance of a region; on a sphere latitudes
    /// are clamped to [-90, 90] and a box reaching a pole or wider than a full turn takes every
    /// longitude, [-180, 180]. Longitudes are not wrapped.
    /// @param region The region, in surface coordinates.
    /// @param metres The distance in metres.
    /// @return The expanded box.
    Bounds2d Expand(const Bounds2d& region, double metres) const;

    /// @brief Whether a region holds a point, half-open as `Bounds2d::Contains`; on a sphere a
    /// longitude also counts 360 degrees either side, and a box reaching latitude 90 holds the
    /// north pole.
    /// @param region The region, in surface coordinates.
    /// @param point The point, in surface coordinates.
    /// @return True if `region` holds `point`.
    bool Contains(const Bounds2d& region, const Vec2d& point) const {
        if (region.Contains(point.x, point.y)) {
            return true;
        }
        if (kind_ == Kind::Plane) {
            return false;
        }
        const Vec2d pole{point.x, point.y >= 90.0 && region.maxY >= 90.0 ? region.minY : point.y};
        return region.Contains(pole.x, pole.y) || region.Contains(pole.x - 360.0, pole.y) ||
               region.Contains(pole.x + 360.0, pole.y);
    }

    /// @brief The 3D bucket holding a point, for hashing nearby features.
    /// @param point The surface point.
    /// @param size The bucket side in metres.
    /// @return The bucket of `Position(point)`; on a plane k is always 0.
    BucketIndex Bucket(const Vec2d& point, double size) const;

    /// @brief Whether two surfaces are the same kind and radius.
    bool operator==(const Surface&) const = default;

  private:
    Vec2d SphereOffset(const Vec2d& centre, const Vec2d& point) const;
    Vec2d SphereLocate(const Vec2d& centre, const Vec2d& offset) const;

    Kind kind_ = Kind::Plane;
    double radius_ = 0.0;
    double unitLength_ = 1.0;
};

/// @brief The unit vector of a longitude and latitude: x towards longitude 0 on the equator, y
/// towards longitude 90 and z towards the north pole.
/// @param lonDeg Longitude in degrees.
/// @param latDeg Latitude in degrees.
/// @return The unit vector.
Vec3d UnitVector(double lonDeg, double latDeg);

}  // namespace terraingen
