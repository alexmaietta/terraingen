#pragma once

#include "terraingen/craters/population.h"

#include <vector>

namespace terraingen {

/// @throws std::invalid_argument unless `diameterRatio` >= 1 and `separation` >= 0 are finite.
void ValidateCraterSpacing(const CraterSpacing& spacing);

/// Whether `a` and `b` are similar in size and closer than `spacing` allows.
bool CratersConflict(const Crater& a, const Crater& b, const CraterSpacing& spacing);

/// Whether `a` takes precedence over `b` when they conflict: a manual crater over a generated one,
/// then the younger crater, which would have overprinted the older, then the larger, then the one
/// with the smaller ID.
bool CraterTakesPrecedence(const Crater& a, const Crater& b);

/// For each crater, whether it conflicts with a crater that takes precedence over it.
///
/// This is Matern type II hard-core thinning: a crater is removed whenever a conflicting crater
/// takes precedence, even if that crater is itself removed, so each decision depends only on
/// craters within reach and does not depend on the order of the list. Manual craters are never
/// marked, so real craters are not thinned even when two of them conflict.
///
/// Reference: B. Matern, "Spatial Variation", Lecture Notes in Statistics 36, Springer, 1986.
/// @throws std::invalid_argument for invalid `spacing`.
std::vector<bool> SpacingConflicts(const CraterList& craters, const CraterSpacing& spacing);

/// `craters` without those `SpacingConflicts` marks, in their original order.
///
/// Craters near the edge of a region can only be judged against the craters the list contains, so
/// for results that join across tiles, thin a list that extends `SpacingReach` beyond the region
/// and crop afterwards (`GenerateCraters` does this when given `spacing`).
/// @throws std::invalid_argument for invalid `spacing`.
CraterList ThinBySpacing(const CraterList& craters, const CraterSpacing& spacing);

/// Farthest distance, in metres, at which a crater at most `diameter` metres across can conflict
/// with another crater at most `maxDiameter` across.
double SpacingReach(double diameter, double maxDiameter, const CraterSpacing& spacing);

}  // namespace terraingen
