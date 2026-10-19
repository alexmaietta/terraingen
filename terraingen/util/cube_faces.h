/// @file
/// @brief The two cube layouts directions are gridded on: `CubeSphereCells`' faces and Gaskell's
/// ICQ faces.
#pragma once

#include "terraingen/util/geometry.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace terraingen::detail {

/// pi / 4 radians, the largest equiangular angle from a face's centre to its edge.
inline constexpr double kQuarter = std::numbers::pi / 4.0;

/// A cube face: its outward normal and the axes its first and second coordinates run along.
struct CubeFace {
    Vec3d normal;
    Vec3d u;
    Vec3d v;
};

/// The faces of `CubeSphereCells`: +x, -x, +y, -y, +z and -z, in order.
constexpr std::array<CubeFace, 6> kCubeFaces{{
    {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}},
    {{-1.0, 0.0, 0.0}, {0.0, -1.0, 0.0}, {0.0, 0.0, 1.0}},
    {{0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}},
    {{0.0, -1.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}},
    {{0.0, 0.0, 1.0}, {0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0}},
    {{0.0, 0.0, -1.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}},
}};

/// Gaskell's ICQ faces 1 to 6, centred on +z, -y, -x, +y, +x and -z, with `u` the direction I runs
/// along and `v` the direction J runs along; measured from his published models.
constexpr std::array<CubeFace, 6> kIcqFaces{{
    {{0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}, {0.0, -1.0, 0.0}},
    {{0.0, -1.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, -1.0}},
    {{-1.0, 0.0, 0.0}, {0.0, -1.0, 0.0}, {0.0, 0.0, -1.0}},
    {{0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0}, {0.0, 0.0, -1.0}},
    {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, -1.0}},
    {{0.0, 0.0, -1.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}},
}};

/// The point `normal + a u + b v` on the plane of `face`, for gnomonic coordinates `a` and `b`.
inline Vec3d CubeFacePoint(const CubeFace& face, double a, double b) {
    return {face.normal.x + a * face.u.x + b * face.v.x,
            face.normal.y + a * face.u.y + b * face.v.y,
            face.normal.z + a * face.u.z + b * face.v.z};
}

/// The index in `faces` of the face a nonzero `direction` points through: the one whose normal it
/// is closest to, the first such face on ties.
inline std::size_t NearestFace(const std::array<CubeFace, 6>& faces, const Vec3d& direction) {
    std::size_t face = 0;
    double best = -2.0;
    for (std::size_t f = 0; f < faces.size(); ++f) {
        const double along = Dot(direction, faces[f].normal);
        if (along > best) {
            best = along;
            face = f;
        }
    }
    return face;
}

/// The equiangular coordinate in [0, `cells`] of gnomonic coordinate `g` along a face split into
/// `cells` equal angles per edge.
inline double EquiangularCoordinate(double g, double cells) {
    return (std::atan(g) + kQuarter) / (2.0 * kQuarter) * cells;
}

}  // namespace terraingen::detail
