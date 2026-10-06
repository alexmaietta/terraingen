#include "terraingen/distribution/spliced.h"

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace terraingen {

namespace {

bool Covers(const SizeFrequencyDistribution& distribution, double low, double high) {
    return distribution.AllowsExtrapolation() ||
           (distribution.ValidRange().min <= low && distribution.ValidRange().max >= high);
}

}  // namespace

SplicedSfd::SplicedSfd(std::vector<SfdPiece> pieces, DiameterRange range, double tolerance)
    : SizeFrequencyDistribution(pieces.empty() || !pieces.front().distribution
                                    ? SfdKind::Density
                                    : pieces.front().distribution->Kind(),
                                range),
      pieces_(std::move(pieces)) {
    if (pieces_.empty()) {
        throw std::invalid_argument("A spliced distribution needs at least one piece");
    }
    if (pieces_.front().start != range.min) {
        throw std::invalid_argument("The first piece must start at the range minimum");
    }
    for (std::size_t i = 0; i < pieces_.size(); ++i) {
        const SfdPiece& piece = pieces_[i];
        if (!piece.distribution || piece.distribution->Kind() != Kind()) {
            throw std::invalid_argument("Spliced pieces must be distributions of one kind");
        }
        const double end = i + 1 < pieces_.size() ? pieces_[i + 1].start : range.max;
        if (i > 0 && !(std::isfinite(piece.start) && piece.start > pieces_[i - 1].start &&
                       piece.start < range.max)) {
            throw std::invalid_argument("Piece starts must increase within the range");
        }
        if (!Covers(*piece.distribution, piece.start, end)) {
            throw std::invalid_argument("Each piece must be valid over its diameter interval");
        }
        if (i == 0) {
            continue;
        }
        const double below = CumulativeOf(*pieces_[i - 1].distribution, piece.start);
        const double above = CumulativeOf(*piece.distribution, piece.start);
        if (above > below * (1.0 + tolerance)) {
            throw std::invalid_argument("A cumulative distribution cannot rise at a splice");
        }
        if (above < below * (1.0 - tolerance)) {
            throw std::invalid_argument("Pieces are discontinuous at " +
                                        std::to_string(piece.start) + " m");
        }
    }
}

std::size_t SplicedSfd::PieceAt(double diameter) const {
    std::size_t index = 0;
    while (index + 1 < pieces_.size() && pieces_[index + 1].start < diameter) {
        ++index;
    }
    return index;
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
