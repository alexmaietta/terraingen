// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/distribution/spliced.h"

#include "distribution/composition.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

/// Whether `distribution` may be evaluated from `low` to `high` meters.
bool Covers(const SizeFrequencyDistribution& distribution, double low, double high) {
    const DiameterRange usable = detail::UsableRange(distribution);
    return usable.min <= low && usable.max >= high;
}

}  // namespace

SplicedSfd::SplicedSfd(std::vector<SfdPiece> pieces, double maxDiameter, double tolerance)
    : SizeFrequencyDistribution(
          detail::CommonKind(detail::Distributions(pieces), "A spliced distribution"),
          detail::PieceRange(pieces, maxDiameter)),
      pieces_(std::move(pieces)) {
    const DiameterRange& range = ValidRange();
    detail::CheckStarts(detail::Starts(pieces_), range, "Piece");
    for (std::size_t i = 0; i < pieces_.size(); ++i) {
        const double end = i + 1 < pieces_.size() ? pieces_[i + 1].start : range.max;
        if (!Covers(*pieces_[i].distribution, pieces_[i].start, end)) {
            throw std::invalid_argument("Each piece must be valid over its diameter interval");
        }
    }
    detail::CheckJoins(
        pieces_,
        [](const SfdPiece& piece, double diameter) {
            return UncheckedCumulative(*piece.distribution, diameter);
        },
        tolerance, false);
}

std::size_t SplicedSfd::PieceAt(double diameter) const {
    return detail::PieceIndex(pieces_, diameter);
}

double SplicedSfd::CumulativeAt(double diameter) const {
    return UncheckedCumulative(*pieces_[PieceAt(diameter)].distribution, diameter);
}

double SplicedSfd::IncrementalAt(double diameter) const {
    return UncheckedIncremental(*pieces_[PieceAt(diameter)].distribution, diameter);
}

double SplicedSfd::InvertCumulative(double target, double minDiameter, double maxDiameter) const {
    const std::size_t index = detail::WalkPieces(
        pieces_, PieceAt(minDiameter), maxDiameter, [&](std::size_t i, double high) {
            return target >= UncheckedCumulative(*pieces_[i].distribution, high);
        });
    const SizeFrequencyDistribution& piece = *pieces_[index].distribution;
    const auto [low, high] = detail::PieceInterval(pieces_, index, minDiameter, maxDiameter);
    if (target >= UncheckedCumulative(piece, low)) {
        return low;
    }
    return UncheckedInverse(piece, target, low, high);
}

}  // namespace terraingen
