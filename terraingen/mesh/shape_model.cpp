#include "terraingen/mesh/shape_model.h"

#include "terraingen/util/arithmetic.h"
#include "terraingen/util/cube_faces.h"
#include "terraingen/util/log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace terraingen {

namespace {

/// The triangles near each patch of a cube grid of directions, by the gnomonic projections of
/// their corners, so each ray tests only the triangles around it.
class TriangleBins {
  public:
    TriangleBins(const TriangleMesh& mesh, std::size_t n) : n_(n), bins_(6 * n * n) {
        for (std::size_t t = 0; t < mesh.triangles.size(); ++t) {
            std::array<Vec3d, 3> corner{};
            for (std::size_t k = 0; k < 3; ++k) {
                corner[k] = mesh.positions[mesh.triangles[t][k]];
            }
            for (std::size_t face = 0; face < 6; ++face) {
                Insert(static_cast<std::uint32_t>(t), face, corner);
            }
        }
    }

    /// The triangles in the patches around patch coordinates (`s`, `t`) of `face`.
    void Near(std::size_t face, double s, double t, std::vector<std::uint32_t>& out) const {
        out.clear();
        const auto range = [&](double x) {
            const double k = std::floor(x);
            return std::array<std::size_t, 2>{ClampIndex(k - 1.0, n_), ClampIndex(k, n_)};
        };
        const auto [i0, i1] = range(s);
        const auto [j0, j1] = range(t);
        for (std::size_t j = j0; j <= j1; ++j) {
            for (std::size_t i = i0; i <= i1; ++i) {
                const auto& bin = bins_[(face * n_ + j) * n_ + i];
                out.insert(out.end(), bin.begin(), bin.end());
            }
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    }

  private:
    void Insert(std::uint32_t triangle, std::size_t face, const std::array<Vec3d, 3>& corner) {
        const detail::CubeFace& f = detail::kCubeFaces[face];
        double sLo = static_cast<double>(n_);
        double sHi = 0.0;
        double tLo = static_cast<double>(n_);
        double tHi = 0.0;
        bool behind = false;
        bool front = false;
        for (const Vec3d& p : corner) {
            const double along = Dot(p, f.normal);
            if (along <= 0.0) {
                behind = true;
                continue;
            }
            front = true;
            const double s =
                detail::EquiangularCoordinate(Dot(p, f.u) / along, static_cast<double>(n_));
            const double t =
                detail::EquiangularCoordinate(Dot(p, f.v) / along, static_cast<double>(n_));
            sLo = std::min(sLo, s);
            sHi = std::max(sHi, s);
            tLo = std::min(tLo, t);
            tHi = std::max(tHi, t);
        }
        if (!front) {
            return;
        }
        if (behind) {
            sLo = 0.0;
            tLo = 0.0;
            sHi = static_cast<double>(n_);
            tHi = static_cast<double>(n_);
        }
        const std::size_t i0 = ClampIndex(std::floor(sLo) - 1.0, n_);
        const std::size_t i1 = ClampIndex(std::floor(sHi) + 1.0, n_);
        const std::size_t j0 = ClampIndex(std::floor(tLo) - 1.0, n_);
        const std::size_t j1 = ClampIndex(std::floor(tHi) + 1.0, n_);
        for (std::size_t j = j0; j <= j1; ++j) {
            for (std::size_t i = i0; i <= i1; ++i) {
                bins_[(face * n_ + j) * n_ + i].push_back(triangle);
            }
        }
    }

    std::size_t n_;
    std::vector<std::vector<std::uint32_t>> bins_;
};

/// The distance along unit `direction` from the origin at which the ray crosses triangle
/// (`a`, `b`, `c`), with a small tolerance at its edges; empty if it misses.
std::optional<double> Crossing(const Vec3d& direction, const Vec3d& a, const Vec3d& b,
                               const Vec3d& c) {
    constexpr double kEdge = 1e-9;
    const Vec3d e1 = b - a;
    const Vec3d e2 = c - a;
    const Vec3d p = Cross(direction, e2);
    const double det = Dot(e1, p);
    if (det == 0.0) {
        return std::nullopt;
    }
    const double inverse = 1.0 / det;
    const Vec3d s{-a.x, -a.y, -a.z};
    const double u = Dot(s, p) * inverse;
    if (u < -kEdge || u > 1.0 + kEdge) {
        return std::nullopt;
    }
    const Vec3d q = Cross(s, e1);
    const double v = Dot(direction, q) * inverse;
    if (v < -kEdge || u + v > 1.0 + kEdge) {
        return std::nullopt;
    }
    const double t = Dot(e2, q) * inverse;
    if (!(t > 0.0)) {
        return std::nullopt;
    }
    return t;
}

}  // namespace

RadialShape ShapeModelFromMesh(const TriangleMesh& mesh, std::size_t q) {
    if (q == 0) {
        throw std::invalid_argument("A radial shape needs Q > 0");
    }
    if (mesh.triangles.empty()) {
        throw std::invalid_argument("A shape model mesh needs triangles");
    }
    const TriangleBins bins(mesh, q);
    std::vector<std::uint32_t> near;
    std::size_t folded = 0;
    const RadialShape shape = RadialShape::Sample(q, [&](const Vec3d& direction) {
        const std::size_t face = detail::NearestFace(detail::kCubeFaces, direction);
        const detail::CubeFace& f = detail::kCubeFaces[face];
        const double along = Dot(direction, f.normal);
        bins.Near(
            face,
            detail::EquiangularCoordinate(Dot(direction, f.u) / along, static_cast<double>(q)),
            detail::EquiangularCoordinate(Dot(direction, f.v) / along, static_cast<double>(q)),
            near);
        double outer = 0.0;
        double inner = INFINITY;
        for (const std::uint32_t t : near) {
            const auto& tri = mesh.triangles[t];
            const std::optional<double> hit = Crossing(
                direction, mesh.positions[tri[0]], mesh.positions[tri[1]], mesh.positions[tri[2]]);
            if (hit) {
                outer = std::max(outer, *hit);
                inner = std::min(inner, *hit);
            }
        }
        if (outer == 0.0) {
            throw std::runtime_error(
                "A ray from the shape model's origin misses it: the mesh must enclose its origin");
        }
        if (outer - inner > 1e-6 * outer) {
            ++folded;
        }
        return outer;
    });
    if (folded > 0) {
        LogWarning("mesh", folded, " of ", shape.Radii().size(),
                   " rays from the origin cross the shape model more than once; it is not "
                   "star-shaped from its origin, so its outermost surface is kept");
    }
    return shape;
}

}  // namespace terraingen
