#include "terraingen/distributions/power_law.h"

#include "checks.h"

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace terraingen {

PowerLaw::PowerLaw(double coefficient, double exponent, SfdKind kind, DiameterRange range)
    : SizeFrequencyDistribution(kind, range), coefficient_(coefficient), exponent_(exponent) {
    if (!detail::PositiveFinite(coefficient_) || !detail::PositiveFinite(exponent_)) {
        throw std::invalid_argument(
            "Power law coefficient and exponent must be positive and finite");
    }
}

double PowerLaw::CumulativeAt(double diameter) const {
    return coefficient_ * std::pow(diameter, -exponent_);
}

double PowerLaw::IncrementalAt(double diameter) const {
    return coefficient_ * exponent_ * std::pow(diameter, -exponent_ - 1.0);
}

double PowerLaw::InvertCumulative(double target, double /*dMin*/, double /*dMax*/) const {
    return std::pow(target / coefficient_, -1.0 / exponent_);
}

PiecewisePowerLaw::PiecewisePowerLaw(std::vector<PowerLawSegment> segments, SfdKind kind,
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
        if (!detail::PositiveFinite(segment.coefficient) ||
            !detail::PositiveFinite(segment.exponent)) {
            throw std::invalid_argument("Segment coefficients and exponents must be positive");
        }
        if (i == 0) {
            continue;
        }
        if (!(std::isfinite(segment.start) && segment.start > segments_[i - 1].start &&
              segment.start < range.max)) {
            throw std::invalid_argument("Segment starts must increase within the range");
        }
        const PowerLawSegment& previous = segments_[i - 1];
        const double below = previous.coefficient * std::pow(segment.start, -previous.exponent);
        const double above = segment.coefficient * std::pow(segment.start, -segment.exponent);
        if (above > below * (1.0 + tolerance)) {
            throw std::invalid_argument("A cumulative distribution cannot rise at a segment join");
        }
        if (!allowDiscontinuities && above < below * (1.0 - tolerance)) {
            throw std::invalid_argument("Segments are discontinuous at " +
                                        std::to_string(segment.start) + " m");
        }
    }
}

const PowerLawSegment& PiecewisePowerLaw::SegmentAt(double diameter) const {
    std::size_t index = 0;
    while (index + 1 < segments_.size() && segments_[index + 1].start < diameter) {
        ++index;
    }
    return segments_[index];
}

double PiecewisePowerLaw::CumulativeAt(double diameter) const {
    const PowerLawSegment& segment = SegmentAt(diameter);
    return segment.coefficient * std::pow(diameter, -segment.exponent);
}

double PiecewisePowerLaw::IncrementalAt(double diameter) const {
    const PowerLawSegment& segment = SegmentAt(diameter);
    return segment.coefficient * segment.exponent * std::pow(diameter, -segment.exponent - 1.0);
}

double PiecewisePowerLaw::InvertCumulative(double target, double dMin, double dMax) const {
    std::size_t index = 0;
    while (index + 1 < segments_.size()) {
        const PowerLawSegment& segment = segments_[index];
        const double high = std::fmin(dMax, segments_[index + 1].start);
        if (std::fmax(dMin, segment.start) < high &&
            target >= segment.coefficient * std::pow(high, -segment.exponent)) {
            break;
        }
        ++index;
    }
    const PowerLawSegment& segment = segments_[index];
    const double low = std::fmax(dMin, segment.start);
    if (target > segment.coefficient * std::pow(low, -segment.exponent)) {
        return low;
    }
    return std::pow(target / segment.coefficient, -1.0 / segment.exponent);
}

double PowerLawCoefficientFromKilometres(double coefficient, double exponent) {
    return coefficient * std::pow(1000.0, exponent) / 1e6;
}

}  // namespace terraingen
