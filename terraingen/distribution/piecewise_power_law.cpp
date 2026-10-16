#include "terraingen/distribution/piecewise_power_law.h"

#include "terraingen/distribution/combined.h"
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
    if (segments_.front().start != range.min) {
        throw std::invalid_argument("The first segment must start at the range minimum");
    }
    for (std::size_t i = 0; i < segments_.size(); ++i) {
        const PowerLawSegment& segment = segments_[i];
        if (!IsPositive(segment.coefficient) || !IsPositive(segment.exponent)) {
            throw std::invalid_argument("Segment coefficients and exponents must be positive");
        }
        if (i == 0) {
            continue;
        }
        if (!(std::isfinite(segment.start) && segment.start > segments_[i - 1].start &&
              segment.start < range.max)) {
            throw std::invalid_argument("Segment starts must increase within the range");
        }
        detail::CheckJoin(segments_[i - 1].Cumulative(segment.start),
                          segment.Cumulative(segment.start), segment.start, tolerance,
                          allowDiscontinuities);
    }
}

const PowerLawSegment& PiecewisePowerLawSfd::SegmentAt(double diameter) const {
    return segments_[detail::IndexOfStart(segments_, diameter)];
}

double PiecewisePowerLawSfd::CumulativeAt(double diameter) const {
    return SegmentAt(diameter).Cumulative(diameter);
}

double PiecewisePowerLawSfd::IncrementalAt(double diameter) const {
    return SegmentAt(diameter).Incremental(diameter);
}

double PiecewisePowerLawSfd::InvertCumulative(double target, double dMin, double dMax) const {
    std::size_t index = 0;
    while (index + 1 < segments_.size()) {
        const PowerLawSegment& segment = segments_[index];
        const double high = std::fmin(dMax, segments_[index + 1].start);
        if (std::fmax(dMin, segment.start) < high && target >= segment.Cumulative(high)) {
            break;
        }
        ++index;
    }
    const PowerLawSegment& segment = segments_[index];
    const double low = std::fmax(dMin, segment.start);
    if (target > segment.Cumulative(low)) {
        return low;
    }
    return segment.Invert(target);
}

}  // namespace terraingen
