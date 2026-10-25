// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `Surface`, the plane, sphere, triaxial ellipsoid or shape model a terrain lies on, with
/// local frames and distances.
#pragma once

#include "terraingen/util/geometry.h"
#include "terraingen/util/radial_shape.h"

#include <memory>

namespace terraingen {

/// The east, north and up unit vectors at a surface point.
struct LocalFrame {
    Vec3d east;   ///< Unit vector east.
    Vec3d north;  ///< Unit vector north.
    Vec3d up;     ///< Unit vector up: the surface normal, away from the center of a body.
};

/// The integer coordinates of a cube of a 3D grid.
struct BucketIndex {
    long long i = 0;
    long long j = 0;
    long long k = 0;

    /// Componentwise equality.
    bool operator==(const BucketIndex& other) const {
        return i == other.i && j == other.j && k == other.k;
    }
    bool operator!=(const BucketIndex& other) const {
        return !(*this == other);
    }
};

/// The plane or body a terrain lies on, measuring distances and directions between surface
/// coordinates: meters on a plane, planetocentric longitude and latitude in degrees on a body.
/// Local frames are azimuthal equidistant on a sphere and tangent planes on an ellipsoid or shape
/// model, offsets projected along the normal.
class Surface {
  public:
    /// A plane, the default.
    Surface() = default;

    /// A plane.
    static Surface Plane() {
        return {};
    }

    /// A sphere of `radius` meters.
    /// @throws std::invalid_argument unless `radius` is positive and finite.
    static Surface Sphere(double radius);

    /// A triaxial ellipsoid with semi-axes `a`, `b` and `c` meters along x (longitude 0), y
    /// (longitude 90) and z (the north pole); equal radii give `Sphere`.
    /// @throws std::invalid_argument unless every radius is positive and finite.
    static Surface Ellipsoid(double a, double b, double c);

    /// A star-shaped body whose radius by direction is `shape`, such as an asteroid from a shape
    /// model; up is the normal of the interpolated surface, from finite differences.
    /// @throws std::invalid_argument for a null `shape`.
    static Surface ShapeModel(std::shared_ptr<const RadialShape> shape);

    /// Whether the surface is a body (a sphere, an ellipsoid or a shape model) rather than a plane.
    bool IsBody() const {
        return form_ != Form::Plane;
    }

    /// Whether the surface is exactly a sphere, of one radius.
    bool IsSphere() const {
        return form_ == Form::Sphere;
    }

    /// The body's shape model; null for a plane, a sphere or an ellipsoid.
    const RadialShape* Shape() const {
        return shape_.get();
    }

    /// The smallest distance in meters from the center to the surface; 0 on a plane.
    double MinRadius() const;

    /// The largest distance in meters from the center to the surface; 0 on a plane.
    double MaxRadius() const;

    /// The radius in meters of a sphere, or the volumetric mean radius of an ellipsoid
    /// ((abc)^(1/3)) or shape model; 0 for a plane.
    double Radius() const {
        return radius_;
    }

    /// The semi-axes in meters along x, y and z; all equal on a sphere, 0 on a plane or a shape
    /// model.
    Vec3d Radii() const {
        return radii_;
    }

    /// The distance in meters from the center to the surface at `point`: the sphere's radius, or
    /// the ellipsoid's radius in the point's direction; 0 on a plane.
    double RadiusAt(const Vec2d& point) const;

    /// The surface area per steradian of direction from the center at `point`, r^2 / (u . n) in
    /// m^2: R^2 on a sphere; 1 on a plane, where it is the area per square meter.
    double AreaPerSteradian(const Vec2d& point) const;

    /// Meters per surface unit along a meridian: 1 on a plane, R pi / 180 on a sphere, and that of
    /// the mean radius `Radius` on an ellipsoid, where the true scale varies with position.
    double UnitLength() const {
        return form_ == Form::Plane ? 1.0 : unitLength_;
    }

    /// The east and north offsets in meters of `point` in the local frame of `center` (both in
    /// surface coordinates); exactly `point` - `center` on a plane.
    Vec2d Offset(const Vec2d& center, const Vec2d& point) const {
        switch (form_) {
        case Form::Plane:
            return point - center;
        case Form::Sphere:
            return SphereOffset(center, point);
        case Form::Ellipsoid:
        case Form::Shape:
            break;
        }
        return TangentOffset(center, point);
    }

    /// The point, in surface coordinates, at east and north `offset` meters from `center`: the
    /// inverse of `Offset`. On a sphere the longitude is in (-180, 180].
    Vec2d Locate(const Vec2d& center, const Vec2d& offset) const {
        switch (form_) {
        case Form::Plane:
            return center + offset;
        case Form::Sphere:
            return SphereLocate(center, offset);
        case Form::Ellipsoid:
            return EllipsoidLocate(center, offset);
        case Form::Shape:
            break;
        }
        return ShapeLocate(center, offset);
    }

    /// The distance in meters between two points: straight on a plane, along a great circle on
    /// a sphere, and the chord on an ellipsoid, which matches the surface distance to second order
    /// for nearby points and never exceeds it.
    double Distance(const Vec2d& a, const Vec2d& b) const;

    /// A surface point in 3D, in meters: (x, y, 0) on a plane; `RadiusAt` times `UnitVector` on a
    /// body.
    Vec3d Position(const Vec2d& point) const;

    /// The east, north and up unit vectors at a point in the frame of `Position`: the x, y
    /// and z axes on a plane; the local frame of `Offset` on a body, with up along the normal.
    LocalFrame Frame(const Vec2d& point) const;

    /// The surface point of a nonzero `direction` from the body's center; (x, y) on a plane.
    Vec2d Coordinates(const Vec3d& direction) const;

    /// A box containing every point within a distance of a region; on a body latitudes
    /// are clamped to [-90, 90] and a box reaching a pole or wider than a full turn takes every
    /// longitude, [-180, 180]. Longitudes are not wrapped.
    /// @param meters The distance in meters.
    Bounds2d Expand(const Bounds2d& region, double meters) const;

    /// Whether a region holds a point, half-open as `Bounds2d::Contains`; on a sphere a
    /// longitude also counts 360 degrees either side, and a box reaching latitude 90 holds the
    /// north pole.
    bool Contains(const Bounds2d& region, const Vec2d& point) const {
        if (region.Contains(point.x, point.y)) {
            return true;
        }
        if (form_ == Form::Plane) {
            return false;
        }
        const Vec2d pole{point.x, point.y >= 90.0 && region.maxY >= 90.0 ? region.minY : point.y};
        return region.Contains(pole.x, pole.y) || region.Contains(pole.x - 360.0, pole.y) ||
               region.Contains(pole.x + 360.0, pole.y);
    }

    /// The 3D bucket holding `Position(point)`, for hashing nearby features; on a plane k is
    /// always 0.
    /// @param size The bucket side in meters.
    BucketIndex Bucket(const Vec2d& point, double size) const;

    /// Whether two surfaces are the same form and radii, and share one shape model or have none.
    bool operator==(const Surface& other) const {
        return form_ == other.form_ && radius_ == other.radius_ &&
               unitLength_ == other.unitLength_ && radii_ == other.radii_ && shape_ == other.shape_;
    }
    bool operator!=(const Surface& other) const {
        return !(*this == other);
    }

  private:
    /// The shape of the surface, set by the factories.
    enum class Form {
        Plane,      ///< A plane.
        Sphere,     ///< A sphere: an ellipsoid of equal radii.
        Ellipsoid,  ///< A triaxial ellipsoid of unequal radii.
        Shape,      ///< A shape model.
    };

    /// Whether the surface is a body the sphere formulas do not apply to: an ellipsoid of unequal
    /// radii or a shape model.
    bool IsNonSphere() const {
        return form_ == Form::Ellipsoid || form_ == Form::Shape;
    }

    Vec2d SphereOffset(const Vec2d& center, const Vec2d& point) const;
    Vec2d SphereLocate(const Vec2d& center, const Vec2d& offset) const;
    Vec2d TangentOffset(const Vec2d& center, const Vec2d& point) const;
    Vec2d EllipsoidLocate(const Vec2d& center, const Vec2d& offset) const;
    Vec2d ShapeLocate(const Vec2d& center, const Vec2d& offset) const;
    /// The distance from `q` along -`up` to the shape model's surface, by bracketing and
    /// bisection.
    double ShapeDepth(const Vec3d& q, const Vec3d& up) const;
    /// The outward unit normal at surface coordinates `point`, whose position is `p`, on an
    /// ellipsoid or shape model.
    Vec3d Normal(const Vec2d& point, const Vec3d& p) const;

    Form form_ = Form::Plane;
    double radius_ = 0.0;
    double unitLength_ = 1.0;
    Vec3d radii_{};
    std::shared_ptr<const RadialShape> shape_;
};

/// The unit vector of a longitude and latitude: x toward longitude 0 on the equator, y
/// toward longitude 90 and z toward the north pole.
/// Both angles are in degrees.
Vec3d UnitVector(double lonDeg, double latDeg);

}  // namespace terraingen
