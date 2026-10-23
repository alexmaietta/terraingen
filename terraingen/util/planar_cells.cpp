// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/util/cells.h"

#include "terraingen/util/checks.h"

#include <cmath>
#include <limits>
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

/// Checks a region: finite and not empty.
void CheckRegion(const Bounds2d& region) {
    if (!(region.IsFinite() && region.maxX > region.minX && region.maxY > region.minY)) {
        throw std::invalid_argument("Region must be finite and not empty");
    }
}

}  // namespace

std::shared_ptr<const SurfaceCells> MakeCells(const Surface& surface) {
    if (surface.IsBody()) {
        return std::make_shared<CubeSphereCells>(surface);
    }
    return std::make_shared<PlanarCells>();
}

std::vector<CellIndex> PlanarCells::CellsCovering(const Bounds2d& region, double cellSize) const {
    RequirePositive(cellSize, "Cell size");
    CheckRegion(region);
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
