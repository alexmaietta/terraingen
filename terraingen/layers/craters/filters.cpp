#include "terraingen/layers/craters/filters.h"

#include "terraingen/util/checks.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace terraingen {

namespace {

using CellKey = std::array<long long, 3>;

struct CellKeyHash {
    std::size_t operator()(const CellKey& key) const {
        std::uint64_t hash = 1469598103934665603ull;
        for (const long long part : key) {
            hash = (hash ^ static_cast<std::uint64_t>(part)) * 1099511628211ull;
        }
        return static_cast<std::size_t>(hash);
    }
};

/// Craters of one diameter band [2^k, 2^(k+1)) hashed into cubic cells (`Surface::Bucket`) at least
/// as wide as any distance at which a crater can conflict with them.
struct BandGrid {
    double cellSize = 0.0;
    std::unordered_map<CellKey, std::vector<std::size_t>, CellKeyHash> cells;
};

CellKey KeyOf(const Crater& crater, double size, const Surface& surface) {
    CellKey key{};
    surface.Bucket({crater.x, crater.y}, size, key[0], key[1], key[2]);
    return key;
}

}  // namespace

void ValidateCraterSpacing(const CraterSpacing& spacing) {
    if (!(std::isfinite(spacing.diameterRatio) && spacing.diameterRatio >= 1.0)) {
        throw std::invalid_argument("Crater spacing diameter ratio must be at least 1");
    }
    RequireNonNegative(spacing.separation, "Crater spacing separation");
}

bool CratersConflict(const Crater& a, const Crater& b, const CraterSpacing& spacing,
                     const Surface& surface) {
    const double larger = std::fmax(a.diameter, b.diameter);
    const double smaller = std::fmin(a.diameter, b.diameter);
    if (larger > spacing.diameterRatio * smaller) {
        return false;
    }
    const double limit = spacing.separation * larger;
    if (surface.IsSphere()) {
        return surface.Distance({a.x, a.y}, {b.x, b.y}) < limit;
    }
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

std::vector<bool> SpacingConflicts(const CraterList& craters, const CraterSpacing& spacing,
                                   const Surface& surface) {
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
        grid.cells[KeyOf(crater, grid.cellSize, surface)].push_back(n);
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
            const CellKey centre = KeyOf(crater, grid.cellSize, surface);
            const long long depth = surface.IsSphere() ? 1 : 0;
            for (long long di = -1; di <= 1 && !conflicts[n]; ++di) {
                for (long long dj = -1; dj <= 1 && !conflicts[n]; ++dj) {
                    for (long long dk = -depth; dk <= depth && !conflicts[n]; ++dk) {
                        const auto cell =
                            grid.cells.find({centre[0] + di, centre[1] + dj, centre[2] + dk});
                        if (cell == grid.cells.end()) {
                            continue;
                        }
                        for (const std::size_t other : cell->second) {
                            if (other == n) {
                                continue;
                            }
                            const Crater neighbour = craters.At(other);
                            if (CratersConflict(crater, neighbour, spacing, surface) &&
                                CraterTakesPrecedence(neighbour, crater)) {
                                conflicts[n] = true;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    return conflicts;
}

CraterList ThinBySpacing(const CraterList& craters, const CraterSpacing& spacing,
                         const Surface& surface) {
    const std::vector<bool> conflicts = SpacingConflicts(craters, spacing, surface);
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
