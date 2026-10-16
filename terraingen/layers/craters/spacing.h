/// @file
/// @brief Keeping craters of similar size apart: conflicts, precedence and thinning.
#pragma once

#include "terraingen/layers/craters/population.h"

#include <vector>

namespace terraingen {

/// @brief Checks crater spacing settings.
/// @param spacing The settings to check.
/// @throws std::invalid_argument unless `diameterRatio` >= 1 and `separation` >= 0 are finite.
void ValidateCraterSpacing(const CraterSpacing& spacing);

/// @brief Whether two craters are similar in size and closer than spacing allows.
/// @param a One crater.
/// @param b The other.
/// @param spacing The spacing settings.
/// @param surface What the craters lie on; distances are measured on it.
/// @return True when they conflict.
bool CratersConflict(const Crater& a, const Crater& b, const CraterSpacing& spacing,
                     const Surface& surface = {});

/// @brief Whether one crater takes precedence over another when they conflict: a manual crater
/// over a generated one, then the younger crater, which would have overprinted the older, then the
/// larger, then the one with the smaller ID.
/// @param a One crater.
/// @param b The other.
/// @return True when `a` wins.
bool CraterTakesPrecedence(const Crater& a, const Crater& b);

/// @brief For each crater, whether it conflicts with a crater that takes precedence over it
/// (Matern type II thinning); manual craters are never marked.
/// @param craters The craters.
/// @param spacing The spacing settings.
/// @param surface What the craters lie on.
/// @return One flag per crater, true for those to remove.
/// @throws std::invalid_argument for invalid `spacing`.
std::vector<bool> SpacingConflicts(const CraterList& craters, const CraterSpacing& spacing,
                                   const Surface& surface = {});

/// @brief Craters without those `SpacingConflicts` marks, in their original order.
///
/// For results that join across tiles, thin a list extending `SpacingReach` beyond the region and
/// crop afterwards, as `GenerateCraters` does.
/// @param craters The craters.
/// @param spacing The spacing settings.
/// @param surface What the craters lie on.
/// @return The craters kept.
/// @throws std::invalid_argument for invalid `spacing`.
CraterList ThinBySpacing(const CraterList& craters, const CraterSpacing& spacing,
                         const Surface& surface = {});

/// @brief How far a crater can conflict with another.
/// @param diameter The crater's largest diameter, in metres.
/// @param maxDiameter The largest diameter of any other crater, in metres.
/// @param spacing The spacing settings.
/// @return The farthest distance of a conflict, in metres.
double SpacingReach(double diameter, double maxDiameter, const CraterSpacing& spacing);

}  // namespace terraingen
