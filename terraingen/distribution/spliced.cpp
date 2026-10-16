#include "terraingen/distribution/spliced.h"

#include "terraingen/distribution/combined.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

/// The kind of `pieces`, after checking there is one and that they share it.
SfdKind CheckedKind(const std::vector<SfdPiece>& pieces) {
    std::vector<const SizeFrequencyDistribution*> distributions;
    for (const SfdPiece& piece : pieces) {
        distributions.push_back(piece.distribution.get());
    }
    return detail::CommonKind(distributions, "A spliced distribution");
}

/// Whether `distribution` may be evaluated from `low` to `high` metres.
bool Covers(const SizeFrequencyDistribution& distribution, double low, double high) {
    const DiameterRange usable = detail::UsableRange(distribution);
    return usable.min <= low && usable.max >= high;
}

}  // namespace

SplicedSfd::SplicedSfd(std::vector<SfdPiece> pieces, DiameterRange range, double tolerance)
    : SizeFrequencyDistribution(CheckedKind(pieces), range), pieces_(std::move(pieces)) {
    if (pieces_.front().start != range.min) {
        throw std::invalid_argument("The first piece must start at the range minimum");
    }
    for (std::size_t i = 0; i < pieces_.size(); ++i) {
        const SfdPiece& piece = pieces_[i];
        const double end = i + 1 < pieces_.size() ? pieces_[i + 1].start : range.max;
        if (i > 0 && !(std::isfinite(piece.start) && piece.start > pieces_[i - 1].start &&
                       piece.start < range.max)) {
            throw std::invalid_argument("Piece starts must increase within the range");
        }
        if (!Covers(*piece.distribution, piece.start, end)) {
            throw std::invalid_argument("Each piece must be valid over its diameter interval");
        }
        if (i > 0) {
            detail::CheckJoin(CumulativeOf(*pieces_[i - 1].distribution, piece.start),
                              CumulativeOf(*piece.distribution, piece.start), piece.start,
                              tolerance, false);
        }
    }
}

std::size_t SplicedSfd::PieceAt(double diameter) const {
    return detail::IndexOfStart(pieces_, diameter);
}

double SplicedSfd::CumulativeAt(double diameter) const {
    return CumulativeOf(*pieces_[PieceAt(diameter)].distribution, diameter);
}

double SplicedSfd::IncrementalAt(double diameter) const {
    return IncrementalOf(*pieces_[PieceAt(diameter)].distribution, diameter);
}

double SplicedSfd::InvertCumulative(double target, double dMin, double dMax) const {
    std::size_t index = PieceAt(dMin);
    while (index + 1 < pieces_.size()) {
        const double high = std::fmin(dMax, pieces_[index + 1].start);
        if (target >= CumulativeOf(*pieces_[index].distribution, high)) {
            break;
        }
        ++index;
    }
    const SizeFrequencyDistribution& piece = *pieces_[index].distribution;
    const double low = std::fmax(dMin, pieces_[index].start);
    const double high =
        index + 1 < pieces_.size() ? std::fmin(dMax, pieces_[index + 1].start) : dMax;
    if (target >= CumulativeOf(piece, low)) {
        return low;
    }
    return InvertCumulativeOf(piece, target, low, high);
}

}  // namespace terraingen
