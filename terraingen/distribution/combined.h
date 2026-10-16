/// @file
/// @brief Checks and lookups shared by the distributions built from others or from pieces.
#pragma once

#include "terraingen/distribution/size_frequency.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace terraingen::detail {

/// The kind every one of `distributions` has.
/// @param what The combination, for messages, such as "A minimum".
/// @throws std::invalid_argument for no inputs, a null input or mixed kinds.
SfdKind CommonKind(std::span<const SizeFrequencyDistribution* const> distributions,
                   const std::string& what);

/// Where `distribution` may be evaluated, in metres: everywhere when it allows extrapolation,
/// otherwise its valid range.
DiameterRange UsableRange(const SizeFrequencyDistribution& distribution);

/// Checks the join at `start` (metres) of a piece giving N = `above` after one giving `below`.
/// @param tolerance The relative mismatch allowed.
/// @param allowDiscontinuities Whether a larger downward jump is allowed.
/// @throws std::invalid_argument for a rise, or a drop beyond `tolerance` unless allowed.
void CheckJoin(double below, double above, double start, double tolerance,
               bool allowDiscontinuities);

/// The index of the piece that applies at `diameter` (metres): the last whose `start` is below it,
/// so the lower piece applies at a join.
/// @param pieces Pieces with increasing `start`.
template <typename Piece>
std::size_t IndexOfStart(const std::vector<Piece>& pieces, double diameter) {
    std::size_t index = 0;
    while (index + 1 < pieces.size() && pieces[index + 1].start < diameter) {
        ++index;
    }
    return index;
}

}  // namespace terraingen::detail
