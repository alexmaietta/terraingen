#include "terraingen/util/cells.h"

#include "terraingen/util/surface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace terraingen {

namespace {

std::int32_t CellCoordinate(double value) {
    constexpr double kLimit = std::numeric_limits<std::int32_t>::max();
    if (!(std::fabs(value) < kLimit)) {
        throw std::invalid_argument("Region is too far from the origin for its cell size");
    }
    return static_cast<std::int32_t>(value);
}

}  // namespace

std::vector<CellIndex> PlanarCells::CellsCovering(const Bounds2d& region, double cellSize) const {
    if (!(std::isfinite(cellSize) && cellSize > 0.0)) {
        throw std::invalid_argument("Cell size must be positive and finite");
    }
    if (!(std::isfinite(region.minX) && std::isfinite(region.minY) && std::isfinite(region.maxX) &&
          std::isfinite(region.maxY) && region.maxX > region.minX && region.maxY > region.minY)) {
        throw std::invalid_argument("Region must be finite and not empty");
    }
    const std::int32_t iMin = CellCoordinate(std::floor(region.minX / cellSize));
    const std::int32_t jMin = CellCoordinate(std::floor(region.minY / cellSize));
    const std::int32_t iMax = CellCoordinate(std::ceil(region.maxX / cellSize) - 1.0);
    const std::int32_t jMax = CellCoordinate(std::ceil(region.maxY / cellSize) - 1.0);
    std::vector<CellIndex> cells;
    cells.reserve(static_cast<std::size_t>(iMax - iMin + 1) *
                  static_cast<std::size_t>(jMax - jMin + 1));
    for (std::int32_t j = jMin; j <= jMax; ++j) {
        for (std::int32_t i = iMin; i <= iMax; ++i) {
            cells.push_back({i, j});
        }
    }
    return cells;
}

double PlanarCells::CellArea(const CellIndex& /*cell*/, double cellSize) const {
    return cellSize * cellSize;
}

Vec2d PlanarCells::SamplePoint(double u, double v, const CellIndex& cell, double cellSize) const {
    return {(cell.i + u) * cellSize, (cell.j + v) * cellSize};
}

}  // namespace terraingen

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
constexpr double kDegree = std::numbers::pi / 180.0;

double Dot(const Vec3d& a, const Vec3d& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/// The solid angle of the gnomonic rectangle [0, `a`] x [0, `b`] on a face, signed.
double Corner(double a, double b) {
    return std::atan(a * b / std::sqrt(1.0 + a * a + b * b));
}

/// The gnomonic coordinate of the edge `k` of `n` cells along a face.
double Edge(std::int32_t k, std::int32_t n) {
    return std::tan(-kQuarter + 2.0 * kQuarter * static_cast<double>(k) / static_cast<double>(n));
}

void CheckCellSize(double cellSize) {
    if (!(std::isfinite(cellSize) && cellSize > 0.0)) {
        throw std::invalid_argument("Cell size must be positive and finite");
    }
}

}  // namespace

CubeSphereCells::CubeSphereCells(double radius) : radius_(radius) {
    if (!(std::isfinite(radius) && radius > 0.0)) {
        throw std::invalid_argument("A sphere's radius must be positive and finite");
    }
}

std::int32_t CubeSphereCells::CellsPerEdge(double cellSize) const {
    CheckCellSize(cellSize);
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

std::vector<CellIndex> CubeSphereCells::CellsCovering(const Bounds2d& region,
                                                      double cellSize) const {
    CheckCellSize(cellSize);
    if (!(std::isfinite(region.minX) && std::isfinite(region.minY) && std::isfinite(region.maxX) &&
          std::isfinite(region.maxY) && region.maxX >= region.minX && region.maxY >= region.minY)) {
        throw std::invalid_argument("Region must be finite and not inverted");
    }
    const Surface sphere = Surface::Sphere(radius_);
    const Bounds2d widened = sphere.Expand(region, cellSize);
    const double step = cellSize / 8.0 / sphere.UnitLength();
    const auto rows = static_cast<std::int64_t>(std::ceil((widened.maxY - widened.minY) / step));
    std::vector<CellIndex> cells;
    for (std::int64_t r = 0; r <= rows; ++r) {
        const double lat = std::min(widened.maxY, widened.minY + static_cast<double>(r) * step);
        const double cosLat = std::cos(lat * kDegree);
        const double width = widened.maxX - widened.minX;
        const auto columns = cosLat * width / step < 1.0
                                 ? std::int64_t{0}
                                 : static_cast<std::int64_t>(std::ceil(width * cosLat / step));
        for (std::int64_t c = 0; c <= columns; ++c) {
            const double lon = columns == 0 ? widened.minX
                                            : widened.minX + width * static_cast<double>(c) /
                                                                 static_cast<double>(columns);
            cells.push_back(CellOf(lon, lat, cellSize));
        }
    }
    std::sort(cells.begin(), cells.end(), [](const CellIndex& a, const CellIndex& b) {
        return a.i != b.i ? a.i < b.i : a.j < b.j;
    });
    cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
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
    const Vec3d p{face.normal.x + a * face.u.x + b * face.v.x,
                  face.normal.y + a * face.u.y + b * face.v.y,
                  face.normal.z + a * face.u.z + b * face.v.z};
    return Surface::Sphere(radius_).PointOf(p);
}

}  // namespace terraingen
