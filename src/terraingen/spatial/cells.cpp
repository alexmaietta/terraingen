#include "terraingen/spatial/cells.h"

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

Vec2d PlanarCells::SamplePoint(RandomStream& random, const CellIndex& cell, double cellSize) const {
    const double u = random.NextDouble();
    const double v = random.NextDouble();
    return {(cell.i + u) * cellSize, (cell.j + v) * cellSize};
}

}  // namespace terraingen
