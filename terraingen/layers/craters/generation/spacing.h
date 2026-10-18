/// @file
/// @brief Keeping craters of similar size apart: conflicts, precedence and thinning.
#pragma once

#include "terraingen/layers/craters/generation/population.h"

#include <limits>
#include <vector>

namespace terraingen {

/// Checks crater spacing settings.
/// @throws std::invalid_argument unless `diameterRatio` >= 1 and `separation` >= 0 are finite.
void ValidateCraterSpacing(const CraterSpacing& spacing);

/// Whether two craters are similar in size and closer than `spacing` allows, measured on
/// `surface`.
bool CratersConflict(const Crater& a, const Crater& b, const CraterSpacing& spacing,
                     const Surface& surface = {});

/// Whether `a` takes precedence over `b` when they conflict: a manual crater over a generated one,
/// then one at least `outrankFrom` metres across over one smaller, then the younger crater (which
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
