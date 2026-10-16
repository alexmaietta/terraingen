/// @file
/// @brief `Surface`, the plane, sphere or triaxial ellipsoid a terrain lies on, with local frames
/// and distances.
#pragma once

#include "terraingen/util/geometry.h"

namespace terraingen {

/// The east, north and up unit vectors at a surface point.
struct LocalFrame {
    Vec3d east;   ///< Unit vector east.
    Vec3d north;  ///< Unit vector north.
    Vec3d up;     ///< Unit vector up: the surface normal, away from the centre of a body.
};

/// The integer coordinates of a cube of a 3D grid.
struct BucketIndex {
    long long i = 0;
    long long j = 0;
    long long k = 0;

    /// Componentwise equality.
    bool operator==(const BucketIndex&) const = default;
};

/// The shape a terrain lies on, a plane or a body (a sphere, or a triaxial ellipsoid of which the
/// sphere is the special case of equal radii), measuring distances and directions between surface
/// coordinates.
///
/// On a plane, surface coordinates are metres in a projected frame. On a body they are
/// planetocentric longitude and latitude in degrees (east and north positive): the direction from
/// the centre, with heights along it. On a sphere of radius R the local frame of a point is its
/// azimuthal equidistant projection in metres; on an ellipsoid it is the tangent plane at the
/// point, onto which offsets are projected along the normal, exact for features small next to the
/// local curvature radius.
class Surface {
  public:
    /// The kind of surface.
    enum class Kind {
        Plane,   ///< A plane.
        Sphere,  ///< A body: a sphere or a triaxial ellipsoid.
    };

    /// A plane, the default.
    Surface() = default;

    /// A plane.
    static Surface Plane() {
        return {};
    }

    /// A sphere of `radius` metres.
    /// @throws std::invalid_argument unless `radius` is positive and finite.
    static Surface Sphere(double radius);

    /// A triaxial ellipsoid with semi-axes `a`, `b` and `c` metres along x (longitude 0), y
    /// (longitude 90) and z (the north pole); equal radii give `Sphere`.
    /// @throws std::invalid_argument unless every radius is positive and finite.
    static Surface Ellipsoid(double a, double b, double c);

    /// The kind of surface.
    Kind GetKind() const {
        return kind_;
    }

    /// Whether the surface is a body, a sphere or an ellipsoid, rather than a plane.
    bool IsSphere() const {
        return kind_ == Kind::Sphere;
    }

    /// The radius in metres of a sphere, or the volumetric mean radius (abc)^(1/3) of an
    /// ellipsoid; 0 for a plane.
    double Radius() const {
        return radius_;
    }

    /// The semi-axes in metres along x, y and z; all equal on a sphere, 0 on a plane.
    Vec3d Radii() const {
        return radii_;
    }

    /// The distance in metres from the centre to the surface at `point`: the sphere's radius, or
    /// the ellipsoid's radius in the point's direction; 0 on a plane.
    double RadiusAt(const Vec2d& point) const;

    /// The surface area per steradian of direction from the centre at `point`, r^2 / (u . n) in
    /// m^2: R^2 on a sphere; 1 on a plane, where it is the area per square metre.
    double AreaPerSteradian(const Vec2d& point) const;

    /// Metres per surface unit along a meridian: 1 on a plane, R pi / 180 on a sphere, and that of
    /// the mean radius `Radius` on an ellipsoid, where the true scale varies with position.
    double UnitLength() const {
        return kind_ == Kind::Plane ? 1.0 : unitLength_;
    }

    /// The east and north offsets in metres of `point` in the local frame of `centre` (both in
    /// surface coordinates); exactly `point` - `centre` on a plane.
    Vec2d Offset(const Vec2d& centre, const Vec2d& point) const {
        if (kind_ == Kind::Plane) {
            return {point.x - centre.x, point.y - centre.y};
        }
        return IsEllipsoid() ? EllipsoidOffset(centre, point) : SphereOffset(centre, point);
    }

    /// The point, in surface coordinates, at east and north `offset` metres from `centre`: the
    /// inverse of `Offset`. On a sphere the longitude is in (-180, 180].
    Vec2d Locate(const Vec2d& centre, const Vec2d& offset) const {
        if (kind_ == Kind::Plane) {
            return {centre.x + offset.x, centre.y + offset.y};
        }
        return IsEllipsoid() ? EllipsoidLocate(centre, offset) : SphereLocate(centre, offset);
    }

    /// The distance in metres between two points: straight on a plane, along a great circle on
    /// a sphere, and the chord on an ellipsoid, which matches the surface distance to second order
    /// for nearby points and never exceeds it.
    double Distance(const Vec2d& a, const Vec2d& b) const;

    /// A surface point in 3D, in metres: (x, y, 0) on a plane; `RadiusAt` times `UnitVector` on a
    /// body.
    Vec3d Position(const Vec2d& point) const;

    /// The east, north and up unit vectors at a point in the frame of `Position`: the x, y
    /// and z axes on a plane; the local frame of `Offset` on a body, with up along the normal.
    LocalFrame Frame(const Vec2d& point) const;

    /// The surface point of a nonzero `direction` from the body's centre; (x, y) on a plane.
    Vec2d PointOf(const Vec3d& direction) const;

    /// A box containing every point within a distance of a region; on a body latitudes
    /// are clamped to [-90, 90] and a box reaching a pole or wider than a full turn takes every
    /// longitude, [-180, 180]. Longitudes are not wrapped.
    /// @param metres The distance in metres.
    Bounds2d Expand(const Bounds2d& region, double metres) const;

    /// Whether a region holds a point, half-open as `Bounds2d::Contains`; on a sphere a
    /// longitude also counts 360 degrees either side, and a box reaching latitude 90 holds the
    /// north pole.
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

    /// The 3D bucket holding `Position(point)`, for hashing nearby features; on a plane k is
    /// always 0.
    /// @param size The bucket side in metres.
    BucketIndex Bucket(const Vec2d& point, double size) const;

    /// Whether two surfaces are the same kind and radii.
    bool operator==(const Surface&) const = default;

  private:
    /// Whether the body has unequal radii, so the sphere formulas do not apply.
    bool IsEllipsoid() const {
        return kind_ == Kind::Sphere && !(radii_.x == radii_.y && radii_.y == radii_.z);
    }

    Vec2d SphereOffset(const Vec2d& centre, const Vec2d& point) const;
    Vec2d SphereLocate(const Vec2d& centre, const Vec2d& offset) const;
    Vec2d EllipsoidOffset(const Vec2d& centre, const Vec2d& point) const;
    Vec2d EllipsoidLocate(const Vec2d& centre, const Vec2d& offset) const;
    /// The outward unit normal of the ellipsoid at its surface point `p`.
    Vec3d EllipsoidNormal(const Vec3d& p) const;

    Kind kind_ = Kind::Plane;
    double radius_ = 0.0;
    double unitLength_ = 1.0;
    Vec3d radii_{};
};

/// The unit vector of a longitude and latitude: x towards longitude 0 on the equator, y
/// towards longitude 90 and z towards the north pole.
/// Both angles are in degrees.
Vec3d UnitVector(double lonDeg, double latDeg);

}  // namespace terraingen
