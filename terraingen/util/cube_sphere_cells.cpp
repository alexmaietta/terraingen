/// @file
/// @brief Near-equal-area cells on the six faces of a cube sphere.
#include "terraingen/util/cells.h"

#include "terraingen/util/checks.h"
#include "terraingen/util/surface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace terraingen {

namespace {

struct Face {
    Vec3d normal;
    Vec3d u;
    Vec3d v;
};

constexpr std::array<Face, 6> kFaces{{
    {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}},
    {{-1.0, 0.0, 0.0}, {0.0, -1.0, 0.0}, {0.0, 0.0, 1.0}},
    {{0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}},
    {{0.0, -1.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}},
    {{0.0, 0.0, 1.0}, {0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0}},
    {{0.0, 0.0, -1.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}},
}};

constexpr double kQuarter = std::numbers::pi / 4.0;

/// The point `normal + a u + b v` on the plane of `face`, for gnomonic coordinates `a` and `b`.
Vec3d FacePoint(const Face& face, double a, double b) {
    return {face.normal.x + a * face.u.x + b * face.v.x,
            face.normal.y + a * face.u.y + b * face.v.y,
            face.normal.z + a * face.u.z + b * face.v.z};
}

/// The solid angle of the gnomonic rectangle [0, `a`] x [0, `b`] on a face, signed.
double Corner(double a, double b) {
    return std::atan(a * b / std::sqrt(1.0 + a * a + b * b));
}

/// The gnomonic coordinate of the edge `k` of `n` cells along a face.
double Edge(std::int32_t k, std::int32_t n) {
    return std::tan(-kQuarter + 2.0 * kQuarter * static_cast<double>(k) / static_cast<double>(n));
}

void SortUnique(std::vector<CellIndex>& cells) {
    std::sort(cells.begin(), cells.end());
    cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
}

}  // namespace

CubeSphereCells::CubeSphereCells(double radius) : radius_(radius) {
    RequirePositive(radius, "A sphere's radius");
}

std::int32_t CubeSphereCells::CellsPerEdge(double cellSize) const {
    RequirePositive(cellSize, "Cell size");
    const double n = std::ceil(radius_ * 2.0 * kQuarter / cellSize);
    if (!(n < static_cast<double>(std::numeric_limits<std::int32_t>::max() / 6))) {
        throw std::invalid_argument("Cells are too small for the sphere");
    }
    return std::max<std::int32_t>(1, static_cast<std::int32_t>(n));
}

CellIndex CubeSphereCells::CellOf(double lonDeg, double latDeg, double cellSize) const {
    const std::int32_t n = CellsPerEdge(cellSize);
    const Vec3d p = UnitVector(lonDeg, latDeg);
    std::size_t face = 0;
    double best = -2.0;
    for (std::size_t f = 0; f < kFaces.size(); ++f) {
        const double along = Dot(p, kFaces[f].normal);
        if (along > best) {
            best = along;
            face = f;
        }
    }
    const auto index = [&](const Vec3d& axis) {
        const double angle = std::atan(Dot(p, axis) / best);
        const auto k = static_cast<std::int32_t>(
            std::floor((angle + kQuarter) / (2.0 * kQuarter) * static_cast<double>(n)));
        return std::clamp<std::int32_t>(k, 0, n - 1);
    };
    return {static_cast<std::int32_t>(face) * n + index(kFaces[face].u), index(kFaces[face].v)};
}

std::vector<CellIndex> CubeSphereCells::Neighbours(const CellIndex& cell, double cellSize,
                                                   int rings) const {
    const std::int32_t n = CellsPerEdge(cellSize);
    const std::int32_t face = cell.i / n;
    const Face& f = kFaces[static_cast<std::size_t>(face)];
    const std::int32_t col = cell.i % n;
    const double step = 2.0 * kQuarter / static_cast<double>(n);
    std::vector<CellIndex> cells;
    for (std::int32_t dj = -rings; dj <= rings; ++dj) {
        for (std::int32_t di = -rings; di <= rings; ++di) {
            const std::int32_t c = col + di;
            const std::int32_t r = cell.j + dj;
            if (c >= 0 && c < n && r >= 0 && r < n) {
                cells.push_back({face * n + c, r});
                continue;
            }
            for (const double su : {0.1, 0.5, 0.9}) {
                for (const double sv : {0.1, 0.5, 0.9}) {
                    const double alpha = -kQuarter + (static_cast<double>(c) + su) * step;
                    const double beta = -kQuarter + (static_cast<double>(r) + sv) * step;
                    if (!(std::fabs(alpha) < 2.0 * kQuarter - step &&
                          std::fabs(beta) < 2.0 * kQuarter - step)) {
                        continue;
                    }
                    const Vec3d p = FacePoint(f, std::tan(alpha), std::tan(beta));
                    const Vec2d point = Surface::Sphere(radius_).PointOf(p);
                    cells.push_back(CellOf(point.x, point.y, cellSize));
                }
            }
        }
    }
    SortUnique(cells);
    return cells;
}

std::vector<CellIndex> CubeSphereCells::CellsCovering(const Bounds2d& region,
                                                      double cellSize) const {
    RequirePositive(cellSize, "Cell size");
    if (!(std::isfinite(region.minX) && std::isfinite(region.minY) && std::isfinite(region.maxX) &&
          std::isfinite(region.maxY) && region.maxX >= region.minX && region.maxY >= region.minY)) {
        throw std::invalid_argument("Region must be finite and not inverted");
    }
    const Surface sphere = Surface::Sphere(radius_);
    const Bounds2d box{region.minX, std::max(-90.0, region.minY), region.maxX,
                       std::min(90.0, region.maxY)};
    const double step = cellSize / 4.0 / sphere.UnitLength();
    const auto rows = static_cast<std::int64_t>(std::ceil((box.maxY - box.minY) / step));
    std::vector<CellIndex> seeds;
    for (std::int64_t r = 0; r <= rows; ++r) {
        const double lat = std::min(box.maxY, box.minY + static_cast<double>(r) * step);
        const double width = std::min(box.maxX - box.minX, 360.0);
        const double cosLat = std::cos(lat * kDegree);
        const auto columns = static_cast<std::int64_t>(std::ceil(width * cosLat / step));
        for (std::int64_t c = 0; c <= columns; ++c) {
            const double lon = columns == 0 ? box.minX
                                            : box.minX + width * static_cast<double>(c) /
                                                             static_cast<double>(columns);
            seeds.push_back(CellOf(lon, lat, cellSize));
        }
    }
    SortUnique(seeds);
    std::vector<CellIndex> cells;
    for (const CellIndex& seed : seeds) {
        const std::vector<CellIndex> around = Neighbours(seed, cellSize, 1);
        cells.insert(cells.end(), around.begin(), around.end());
    }
    SortUnique(cells);
    return cells;
}

double CubeSphereCells::CellArea(const CellIndex& cell, double cellSize) const {
    const std::int32_t n = CellsPerEdge(cellSize);
    const std::int32_t col = cell.i % n;
    const double a1 = Edge(col, n);
    const double a2 = Edge(col + 1, n);
    const double b1 = Edge(cell.j, n);
    const double b2 = Edge(cell.j + 1, n);
    const double solid = Corner(a2, b2) - Corner(a1, b2) - Corner(a2, b1) + Corner(a1, b1);
    return radius_ * radius_ * solid;
}

Vec2d CubeSphereCells::SamplePoint(double u, double v, const CellIndex& cell,
                                   double cellSize) const {
    const std::int32_t n = CellsPerEdge(cellSize);
    const Face& face = kFaces[static_cast<std::size_t>(cell.i / n)];
    const std::int32_t col = cell.i % n;
    const double a1 = Edge(col, n);
    const double a2 = Edge(col + 1, n);
    const double b1 = Edge(cell.j, n);
    const double b2 = Edge(cell.j + 1, n);
    const auto strip = [&](double a) {
        return Corner(a, b2) - Corner(a, b1) - Corner(a1, b2) + Corner(a1, b1);
    };
    const double total = strip(a2);
    double lo = a1;
    double hi = a2;
    for (int iteration = 0; iteration < 64; ++iteration) {
        const double mid = 0.5 * (lo + hi);
        (strip(mid) < u * total ? lo : hi) = mid;
    }
    const double a = 0.5 * (lo + hi);
    const auto ratio = [&](double b) { return b / std::sqrt(1.0 + a * a + b * b); };
    const double k = ratio(b1) + v * (ratio(b2) - ratio(b1));
    const double b = k * std::sqrt(1.0 + a * a) / std::sqrt(1.0 - k * k);
    return Surface::Sphere(radius_).PointOf(FacePoint(face, a, b));
}

Vec3d CubeSphereCells::Direction(int face, double alpha, double beta) {
    const Face& f = kFaces[static_cast<std::size_t>(std::clamp(face, 0, 5))];
    const Vec3d p = FacePoint(f, std::tan(alpha), std::tan(beta));
    const double length = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    return {p.x / length, p.y / length, p.z / length};
}

}  // namespace terraingen
