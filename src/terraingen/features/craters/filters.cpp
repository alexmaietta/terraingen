#include "terraingen/features/craters/filters.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace terraingen {

namespace {

std::int64_t Cell(double coordinate, double cellSize) {
    return static_cast<std::int64_t>(std::floor(coordinate / cellSize));
}

std::int64_t CellKey(std::int64_t i, std::int64_t j) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(i) << 32) ^ (j & 0xffffffff);
}

/// Craters of one diameter band [2^k, 2^(k+1)) hashed into square cells at least as wide as any
/// distance at which a crater can conflict with them.
struct BandGrid {
    double cellSize = 0.0;
    std::unordered_map<std::int64_t, std::vector<std::size_t>> cells;
};

}  // namespace

void ValidateCraterSpacing(const CraterSpacing& spacing) {
    if (!(std::isfinite(spacing.diameterRatio) && spacing.diameterRatio >= 1.0)) {
        throw std::invalid_argument("Crater spacing diameter ratio must be at least 1");
    }
    if (!(std::isfinite(spacing.separation) && spacing.separation >= 0.0)) {
        throw std::invalid_argument("Crater spacing separation must be finite and not negative");
    }
}

bool CratersConflict(const Crater& a, const Crater& b, const CraterSpacing& spacing) {
    const double larger = std::fmax(a.diameter, b.diameter);
    const double smaller = std::fmin(a.diameter, b.diameter);
    if (larger > spacing.diameterRatio * smaller) {
        return false;
    }
    const double limit = spacing.separation * larger;
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy < limit * limit;
}

bool CraterTakesPrecedence(const Crater& a, const Crater& b) {
    if (a.manual != b.manual) {
        return a.manual;
    }
    if (a.ageGyr != b.ageGyr) {
        return a.ageGyr < b.ageGyr;
    }
    if (a.diameter != b.diameter) {
        return a.diameter > b.diameter;
    }
    return a.id < b.id;
}

double SpacingReach(double diameter, double maxDiameter, const CraterSpacing& spacing) {
    return spacing.separation * std::fmin(spacing.diameterRatio * diameter, maxDiameter);
}

std::vector<bool> SpacingConflicts(const CraterList& craters, const CraterSpacing& spacing) {
    ValidateCraterSpacing(spacing);
    std::vector<bool> conflicts(craters.Size(), false);
    if (!spacing.Enabled() || craters.Size() < 2) {
        return conflicts;
    }
    std::map<int, BandGrid> bands;
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        const Crater crater = craters.At(n);
        const int band = std::ilogb(crater.diameter);
        BandGrid& grid = bands[band];
        if (grid.cellSize == 0.0) {
            grid.cellSize = spacing.separation * spacing.diameterRatio * std::ldexp(1.0, band + 1);
        }
        grid.cells[CellKey(Cell(crater.x, grid.cellSize), Cell(crater.y, grid.cellSize))].push_back(
            n);
    }
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        const Crater crater = craters.At(n);
        if (crater.manual) {
            continue;
        }
        const int lowest = std::ilogb(crater.diameter / spacing.diameterRatio);
        const int highest = std::ilogb(crater.diameter * spacing.diameterRatio);
        for (int band = lowest; band <= highest && !conflicts[n]; ++band) {
            const auto found = bands.find(band);
            if (found == bands.end()) {
                continue;
            }
            const BandGrid& grid = found->second;
            const std::int64_t ci = Cell(crater.x, grid.cellSize);
            const std::int64_t cj = Cell(crater.y, grid.cellSize);
            for (std::int64_t di = -1; di <= 1 && !conflicts[n]; ++di) {
                for (std::int64_t dj = -1; dj <= 1 && !conflicts[n]; ++dj) {
                    const auto cell = grid.cells.find(CellKey(ci + di, cj + dj));
                    if (cell == grid.cells.end()) {
                        continue;
                    }
                    for (const std::size_t other : cell->second) {
                        if (other == n) {
                            continue;
                        }
                        const Crater neighbour = craters.At(other);
                        if (CratersConflict(crater, neighbour, spacing) &&
                            CraterTakesPrecedence(neighbour, crater)) {
                            conflicts[n] = true;
                            break;
                        }
                    }
                }
            }
        }
    }
    return conflicts;
}

CraterList ThinBySpacing(const CraterList& craters, const CraterSpacing& spacing) {
    const std::vector<bool> conflicts = SpacingConflicts(craters, spacing);
    CraterList kept;
    kept.Reserve(craters.Size());
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        if (!conflicts[n]) {
            kept.Append(craters.At(n));
        }
    }
    return kept;
}

}  // namespace terraingen
