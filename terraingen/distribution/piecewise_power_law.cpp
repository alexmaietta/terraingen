#include "terraingen/distribution/piecewise_power_law.h"

#include "terraingen/distribution/composition.h"
#include "terraingen/util/checks.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

PiecewisePowerLawSfd::PiecewisePowerLawSfd(std::vector<PowerLawSegment> segments, SfdKind kind,
                                           DiameterRange range, bool allowDiscontinuities,
                                           double tolerance)
    : SizeFrequencyDistribution(kind, range), segments_(std::move(segments)) {
    if (segments_.empty()) {
        throw std::invalid_argument("A piecewise power law needs at least one segment");
    }
    for (const PowerLawSegment& segment : segments_) {
        if (!IsPositive(segment.coefficient) || !IsPositive(segment.exponent)) {
            throw std::invalid_argument("Segment coefficients and exponents must be positive");
        }
    }
    detail::CheckStarts(detail::Starts(segments_), range, "segment");
    detail::CheckJoins(
        segments_,
        [](const PowerLawSegment& segment, double diameter) {
            return segment.Cumulative(diameter);
        },
        tolerance, allowDiscontinuities);
}

const PowerLawSegment& PiecewisePowerLawSfd::SegmentAt(double diameter) const {
    return segments_[detail::PieceIndex(segments_, diameter)];
}

double PiecewisePowerLawSfd::CumulativeAt(double diameter) const {
    return SegmentAt(diameter).Cumulative(diameter);
}

double PiecewisePowerLawSfd::IncrementalAt(double diameter) const {
    return SegmentAt(diameter).Incremental(diameter);
}

double PiecewisePowerLawSfd::InvertCumulative(double target, double dMin, double dMax) const {
    const std::size_t index =
        detail::WalkPieces(segments_, 0, dMax, [&](std::size_t i, double high) {
            const PowerLawSegment& segment = segments_[i];
            return std::fmax(dMin, segment.start) < high && target >= segment.Cumulative(high);
        });
    const PowerLawSegment& segment = segments_[index];
    const double low = std::fmax(dMin, segment.start);
    if (target > segment.Cumulative(low)) {
        return low;
    }
    return segment.Invert(target);
}

}  // namespace terraingen
