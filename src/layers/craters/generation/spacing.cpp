// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/generation/spacing.h"

#include "terraingen/layers/craters/band_buckets.h"
#include "util/checks.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

namespace {

/// Finds whether crater `n` conflicts with a crater that takes precedence over it, through
/// buckets per diameter band at least as wide as any distance at which a crater can conflict.
class ConflictSearch {
  public:
    ConflictSearch(const CraterList& craters, const CraterSpacing& spacing, const Surface& surface)
        : craters_(craters), spacing_(spacing), surface_(surface), buckets_(surface) {
        for (std::size_t n = 0; n < craters.Size(); ++n) {
            const Crater crater = craters.At(n);
            const int band = std::ilogb(crater.diameter);
            buckets_.Add(band,
                         spacing.separation * spacing.diameterRatio * std::ldexp(1.0, band + 1),
                         {crater.x, crater.y}, n);
        }
    }

    bool Outranked(std::size_t n) const {
        const Crater crater = craters_.At(n);
        const int lowest = std::ilogb(crater.diameter / spacing_.diameterRatio);
        const int highest = std::ilogb(crater.diameter * spacing_.diameterRatio);
        bool outranked = false;
        for (int band = lowest; band <= highest && !outranked; ++band) {
            buckets_.ForEachAround(band, {crater.x, crater.y}, 1,
                                   [&](const std::vector<std::size_t>& others) {
                                       outranked = outranked || OutrankedBy(others, n, crater);
                                   });
        }
        return outranked;
    }

  private:
    /// Whether one of `others` conflicts with `crater` and takes precedence over it.
    bool OutrankedBy(const std::vector<std::size_t>& others, std::size_t n,
                     const Crater& crater) const {
        for (const std::size_t other : others) {
            if (other == n) {
                continue;
            }
            const Crater neighbor = craters_.At(other);
            if (CratersConflict(crater, neighbor, spacing_, surface_) &&
                CraterTakesPrecedence(neighbor, crater, spacing_.outrankFrom)) {
                return true;
            }
        }
        return false;
    }

    const CraterList& craters_;
    const CraterSpacing& spacing_;
    const Surface& surface_;
    BandBuckets buckets_;
};

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
    if (surface.IsBody()) {
        return surface.Distance({a.x, a.y}, {b.x, b.y}) < limit;
    }
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy < limit * limit;
}

bool CraterTakesPrecedence(const Crater& a, const Crater& b, double outrankFrom) {
    if (a.manual != b.manual) {
        return a.manual;
    }
    if ((a.diameter >= outrankFrom) != (b.diameter >= outrankFrom)) {
        return a.diameter >= outrankFrom;
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
    const ConflictSearch search(craters, spacing, surface);
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        conflicts[n] = !craters.At(n).manual && search.Outranked(n);
    }
    return conflicts;
}

CraterList ThinBySpacing(const CraterList& craters, const CraterSpacing& spacing,
                         const Surface& surface) {
    const std::vector<bool> conflicts = SpacingConflicts(craters, spacing, surface);
    return FilterCraters(craters, [&](std::size_t n, const Crater&) { return !conflicts[n]; });
}

}  // namespace terraingen
