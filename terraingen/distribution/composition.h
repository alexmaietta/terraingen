/// @file
/// @brief Checks and lookups shared by distributions composed of others or of pieces.
#pragma once

#include "terraingen/distribution/size_frequency.h"
#include "terraingen/util/span.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace terraingen::detail {

/// Whether `Part` has a `distribution` member.
template <typename Part, typename = void> struct HasDistribution : std::false_type {};

template <typename Part>
struct HasDistribution<Part, std::void_t<decltype(std::declval<const Part&>().distribution)>>
    : std::true_type {};

/// The distributions of `parts`: shared pointers, or structs holding one as `distribution`.
template <typename Part>
std::vector<const SizeFrequencyDistribution*> Distributions(const std::vector<Part>& parts) {
    std::vector<const SizeFrequencyDistribution*> distributions;
    distributions.reserve(parts.size());
    for (const Part& part : parts) {
        if constexpr (HasDistribution<Part>::value) {
            distributions.push_back(part.distribution.get());
        } else {
            distributions.push_back(part.get());
        }
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

/// Checks the starts of pieces in meters: the first at `range.min`, the rest finite and strictly
/// increasing below `range.max`.
/// @param noun What a piece is called in messages, such as "segment".
/// @throws std::invalid_argument for no starts or starts that break those rules.
void CheckStarts(Span<const double> starts, const DiameterRange& range, const std::string& noun);

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
/// piece's interval capped at `dMax` meters; the last piece if none does.
template <typename Piece, typename Holds>
std::size_t WalkPieces(const std::vector<Piece>& pieces, std::size_t index, double dMax,
                       const Holds& holds) {
    while (index + 1 < pieces.size() && !holds(index, std::fmin(dMax, pieces[index + 1].start))) {
        ++index;
    }
    return index;
}

/// The interval in meters of piece `index` within [`dMin`, `dMax`].
template <typename Piece>
std::pair<double, double> PieceInterval(const std::vector<Piece>& pieces, std::size_t index,
                                        double dMin, double dMax) {
    const double high = index + 1 < pieces.size() ? std::fmin(dMax, pieces[index + 1].start) : dMax;
    return {std::fmax(dMin, pieces[index].start), high};
}

}  // namespace terraingen::detail
