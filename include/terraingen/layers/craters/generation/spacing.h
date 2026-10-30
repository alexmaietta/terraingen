// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Keeping craters of similar size apart: conflicts, precedence and thinning.
#pragma once

#include "terraingen/geometry/surface.h"
#include "terraingen/layers/craters/crater.h"

#include <limits>
#include <vector>

namespace terraingen {

/// Keeps craters of similar size apart: two conflict when their diameters differ by at most
/// `diameterRatio` and their centers are closer than `separation` times the larger diameter.
struct CraterSpacing {
    CraterSpacing() = default;
    /// Spacing with `diameterRatio` `ratio` and `separation` `minSeparation`.
    CraterSpacing(double ratio, double minSeparation)
        : diameterRatio(ratio), separation(minSeparation) {}

    double diameterRatio = 1.5;  ///< At least 1.
    double separation = 0.0;     ///< In larger diameters; 0 disables spacing.
    /// Craters at least this large (m) take precedence over smaller ones whatever their ages, as a
    /// crater layer sets for the craters that throw secondaries a total map measures; infinite
    /// ranks by age first. Not part of a configuration.
    double outrankFrom = std::numeric_limits<double>::infinity();

    /// Whether spacing applies: a positive `separation`.
    bool Enabled() const {
        return separation > 0.0;
    }

    /// Memberwise equality.
    bool operator==(const CraterSpacing& other) const {
        return diameterRatio == other.diameterRatio && separation == other.separation &&
               outrankFrom == other.outrankFrom;
    }
    bool operator!=(const CraterSpacing& other) const {
        return !(*this == other);
    }
};

/// Checks crater spacing settings.
/// @throws std::invalid_argument for a field outside its range (`CheckRanges`).
void ValidateCraterSpacing(const CraterSpacing& spacing);

/// Whether two craters are similar in size and closer than `spacing` allows, measured on
/// `surface`.
bool CratersConflict(const Crater& a, const Crater& b, const CraterSpacing& spacing,
                     const Surface& surface = {});

/// Whether `a` takes precedence over `b` when they conflict: a manual crater over a generated one,
/// then one at least `outrankFrom` meters across over one smaller, then the younger crater (which
/// would have overprinted the older), then the larger, then the smaller ID.
bool CraterTakesPrecedence(const Crater& a, const Crater& b,
                           double outrankFrom = std::numeric_limits<double>::infinity());

/// One flag per crater, true where it conflicts with a crater that takes precedence over it
/// (Matern type II thinning); manual craters are never marked.
/// @throws std::invalid_argument for invalid `spacing`.
std::vector<bool> SpacingConflicts(const CraterList& craters, const CraterSpacing& spacing,
                                   const Surface& surface = {});

/// The craters `SpacingConflicts` does not mark, in their original order.
///
/// For results that join across tiles, thin a list extending `SpacingReach` beyond the region and
/// crop afterwards, as `GenerateCraters` does.
/// @throws std::invalid_argument for invalid `spacing`.
CraterList ThinBySpacing(const CraterList& craters, const CraterSpacing& spacing,
                         const Surface& surface = {});

/// The farthest distance (m) at which a crater of `diameter` can conflict with another, given
/// the largest diameter `maxDiameter` of any other (m).
double SpacingReach(double diameter, double maxDiameter, const CraterSpacing& spacing);

}  // namespace terraingen
