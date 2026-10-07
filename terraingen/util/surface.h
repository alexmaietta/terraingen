#pragma once

#include "terraingen/util/geometry.h"

namespace terraingen {

/// The shape a terrain lies on: a plane, or a sphere such as a planet or moon. It says what surface
/// coordinates mean and measures distances and directions between them, so features defined in a
/// local frame around their centre (crater profiles, rocks, rays) work on either.
///
/// - On a plane, surface coordinates are metres in a projected frame, and the local frame of a
///   point is that frame shifted to it.
/// - On a sphere of radius R, surface coordinates are longitude and latitude in degrees (east and
///   north positive), as a geographic raster's transform gives them, and the local frame of a point
///   is its azimuthal equidistant projection: east and north in metres, with the distance and
///   azimuth from the point exact.
///
/// A value type rather than a class hierarchy, so the plane path stays inline in inner loops.
class Surface {
  public:
    enum class Kind { Plane, Sphere };

    /// A plane, the default.
    Surface() = default;

    static Surface Plane() {
        return {};
    }

    /// A sphere of radius `radius` metres.
    /// @throws std::invalid_argument unless `radius` is positive and finite.
    static Surface Sphere(double radius);

    Kind GetKind() const {
        return kind_;
    }

    bool IsSphere() const {
        return kind_ == Kind::Sphere;
    }

    /// The sphere's radius in metres; 0 for a plane.
    double Radius() const {
        return radius_;
    }

    /// Metres per surface unit along a meridian: 1 on a plane, R pi / 180 on a sphere.
    double UnitLength() const {
        return kind_ == Kind::Plane ? 1.0 : unitLength_;
    }

    /// `point` in the local east-north frame of `centre`, in metres. On a plane, `point` -
    /// `centre`, exactly.
    Vec2d Offset(const Vec2d& centre, const Vec2d& point) const {
        if (kind_ == Kind::Plane) {
            return {point.x - centre.x, point.y - centre.y};
        }
        return SphereOffset(centre, point);
    }

    /// The point at `offset` metres east and north of `centre` in its local frame: the inverse of
    /// `Offset`. On a sphere the longitude is in (-180, 180].
    Vec2d Locate(const Vec2d& centre, const Vec2d& offset) const {
        if (kind_ == Kind::Plane) {
            return {centre.x + offset.x, centre.y + offset.y};
        }
        return SphereLocate(centre, offset);
    }

    /// The distance in metres from `a` to `b`: straight on a plane, along a great circle on a
    /// sphere.
    double Distance(const Vec2d& a, const Vec2d& b) const;

    /// The point in 3D: (x, y, 0) on a plane, and on a sphere R times the unit vector, with x
    /// towards longitude 0 on the equator, y towards longitude 90 and z towards the north pole.
    Vec3d Position(const Vec2d& point) const;

    /// The surface point of a direction from the sphere's centre (any length but 0); on a plane,
    /// (x, y).
    Vec2d PointOf(const Vec3d& direction) const;

    /// A box containing every point within `metres` of `region`. On a sphere, latitudes are clamped
    /// to [-90, 90], longitudes widen with latitude, and a box reaching a pole or wider than a full
    /// turn takes every longitude, [-180, 180]. Longitudes are not wrapped.
    Bounds2d Expand(const Bounds2d& region, double metres) const;

    /// Whether `region` holds `point`, half-open as `Bounds2d::Contains`; on a sphere a longitude
    /// also counts 360 degrees either side, so boxes running past the antimeridian hold the points
    /// beyond it, and a box reaching latitude 90 holds the north pole.
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

    /// The 3D bucket of side `size` metres holding `point`, for hashing nearby features: on a
    /// plane the z index is always 0.
    void Bucket(const Vec2d& point, double size, long long& i, long long& j, long long& k) const;

    bool operator==(const Surface&) const = default;

  private:
    Vec2d SphereOffset(const Vec2d& centre, const Vec2d& point) const;
    Vec2d SphereLocate(const Vec2d& centre, const Vec2d& offset) const;

    Kind kind_ = Kind::Plane;
    double radius_ = 0.0;
    double unitLength_ = 1.0;
};

/// The unit vector of longitude `lonDeg` and latitude `latDeg`, as `Surface::Position` orients it.
Vec3d UnitVector(double lonDeg, double latDeg);

}  // namespace terraingen
