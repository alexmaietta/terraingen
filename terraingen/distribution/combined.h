/// @file
/// @brief Checks and lookups shared by the distributions built from others or from pieces.
#pragma once

#include "terraingen/distribution/size_frequency.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace terraingen::detail {

/// @brief The kind every one of `distributions` has.
/// @param distributions The inputs of a combined distribution.
/// @param what The combination, for messages, such as "A minimum".
/// @return Their common kind.
/// @throws std::invalid_argument for no inputs, a null input or mixed kinds.
SfdKind CommonKind(std::span<const SizeFrequencyDistribution* const> distributions,
                   const std::string& what);

/// @brief Where `distribution` may be evaluated: everywhere when it allows extrapolation, otherwise
/// its valid range. `SummedSfd` and `SplicedSfd` use this; `MinimumSfd` intersects the inputs' own
/// valid ranges even when they allow extrapolation.
/// @param distribution The distribution.
/// @return The diameters in metres where it may be evaluated.
DiameterRange UsableRange(const SizeFrequencyDistribution& distribution);

/// @brief Checks the join at `start` of a piece giving `above` after one giving `below`.
/// @param below N(>=start) of the piece before the join.
/// @param above N(>=start) of the piece after it.
/// @param start The join diameter in metres.
/// @param tolerance The relative mismatch allowed.
/// @param allowDiscontinuities Whether a larger downward jump is allowed.
/// @throws std::invalid_argument for a rise, or a drop beyond `tolerance` unless allowed.
void CheckJoin(double below, double above, double start, double tolerance,
               bool allowDiscontinuities);

/// @brief The index of the piece that applies at `diameter`: the last whose `start` is below it,
/// so the lower piece applies at a join.
/// @param pieces Pieces with increasing `start`.
/// @param diameter The diameter in metres.
/// @return The index, 0 at or below the second piece's start.
template <typename Piece>
std::size_t IndexOfStart(const std::vector<Piece>& pieces, double diameter) {
    std::size_t index = 0;
    while (index + 1 < pieces.size() && pieces[index + 1].start < diameter) {
        ++index;
    }
    return index;
}

}  // namespace terraingen::detail
