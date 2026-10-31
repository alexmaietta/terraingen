// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Checks and lookups shared by distributions composed of others or of pieces.
#pragma once

#include "terraingen/distribution/size_frequency.h"
#include "terraingen/util/span.h"

#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace terraingen::detail {

/// The distribution a shared pointer holds.
inline const SizeFrequencyDistribution*
Distribution(const std::shared_ptr<const SizeFrequencyDistribution>& part) {
    return part.get();
}

/// The distribution a part holds as its `distribution` member.
template <typename Part> const SizeFrequencyDistribution* Distribution(const Part& part) {
    return part.distribution.get();
}

/// The distributions of `parts`: shared pointers, or structs holding one as `distribution`.
template <typename Part>
std::vector<const SizeFrequencyDistribution*> Distributions(const std::vector<Part>& parts) {
    std::vector<const SizeFrequencyDistribution*> distributions;
    distributions.reserve(parts.size());
    for (const Part& part : parts) {
        distributions.push_back(Distribution(part));
    }
    return distributions;
}

/// The kind every one of `distributions` has.
/// @param what The combination, for messages, such as "A minimum".
/// @throws std::invalid_argument for no inputs, a null input or mixed kinds.
SfdKind CommonKind(Span<const SizeFrequencyDistribution* const> distributions,
                   const std::string& what);

/// Where `distribution` may be evaluated, in meters: everywhere when it allows extrapolation,
/// otherwise its valid range.
DiameterRange UsableRange(const SizeFrequencyDistribution& distribution);

/// The diameters in meters every one of `ranges` holds.
/// @throws std::invalid_argument with `message` if no diameter is in all of them.
DiameterRange IntersectRanges(Span<const DiameterRange> ranges, const std::string& message);

/// The diameters in meters where `range(distribution)` holds for every distribution of `parts`.
/// @throws std::invalid_argument with `message` if no diameter is in all of them.
template <typename Part, typename Range>
DiameterRange IntersectRanges(const std::vector<Part>& parts, const Range& range,
                              const std::string& message) {
    std::vector<DiameterRange> ranges;
    ranges.reserve(parts.size());
    for (const Part& part : parts) {
        ranges.push_back(range(*Distribution(part)));
    }
    return IntersectRanges(ranges, message);
}

/// Checks the starts of pieces in meters: the first at `range.min`, the rest finite and strictly
/// increasing below `range.max`.
/// @param what What the pieces are called in messages, such as "Segment".
/// @throws std::invalid_argument for no starts or starts that break those rules.
void CheckStarts(Span<const double> starts, const DiameterRange& range, const std::string& what);

/// The valid range of a distribution made of `pieces`: from the first one's `start` (0 without
/// pieces) to `maxDiameter`.
template <typename Piece>
DiameterRange PieceRange(const std::vector<Piece>& pieces, double maxDiameter) {
    return {pieces.empty() ? 0.0 : pieces.front().start, maxDiameter};
}

/// The `start` of each of `pieces`.
template <typename Piece> std::vector<double> Starts(const std::vector<Piece>& pieces) {
    std::vector<double> starts;
    starts.reserve(pieces.size());
    for (const Piece& piece : pieces) {
        starts.push_back(piece.start);
    }
    return starts;
}

/// Checks the join at `start` (meters) of a piece giving N = `above` after one giving `below`.
/// @param tolerance The relative mismatch allowed.
/// @param allowDiscontinuities Whether a larger downward jump is allowed.
/// @throws std::invalid_argument for a rise, or a drop beyond `tolerance` unless allowed.
void CheckJoin(double below, double above, double start, double tolerance,
               bool allowDiscontinuities);

/// `CheckJoin` at every join of `pieces`, each evaluated as `cumulative(piece, diameter)`.
template <typename Piece, typename Cumulative>
void CheckJoins(const std::vector<Piece>& pieces, const Cumulative& cumulative, double tolerance,
                bool allowDiscontinuities) {
    for (std::size_t i = 1; i < pieces.size(); ++i) {
        const double start = pieces[i].start;
        CheckJoin(cumulative(pieces[i - 1], start), cumulative(pieces[i], start), start, tolerance,
                  allowDiscontinuities);
    }
}

/// The index of the piece that applies at `diameter` (meters): the last whose `start` is below it,
/// so the lower piece applies at a join.
/// @param pieces Pieces with increasing `start`.
template <typename Piece>
std::size_t PieceIndex(const std::vector<Piece>& pieces, double diameter) {
    std::size_t index = 0;
    while (index + 1 < pieces.size() && pieces[index + 1].start < diameter) {
        ++index;
    }
    return index;
}

/// The first piece from `index` on that `holds(index, high)` accepts, with `high` the end of the
/// piece's interval capped at `maxDiameter` meters; the last piece if none does.
template <typename Piece, typename Holds>
std::size_t WalkPieces(const std::vector<Piece>& pieces, std::size_t index, double maxDiameter,
                       const Holds& holds) {
    while (index + 1 < pieces.size() &&
           !holds(index, std::fmin(maxDiameter, pieces[index + 1].start))) {
        ++index;
    }
    return index;
}

/// The interval in meters of piece `index` within [`minDiameter`, `maxDiameter`].
template <typename Piece>
std::pair<double, double> PieceInterval(const std::vector<Piece>& pieces, std::size_t index,
                                        double minDiameter, double maxDiameter) {
    const double high =
        index + 1 < pieces.size() ? std::fmin(maxDiameter, pieces[index + 1].start) : maxDiameter;
    return {std::fmax(minDiameter, pieces[index].start), high};
}

}  // namespace terraingen::detail
