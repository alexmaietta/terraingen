#include "terraingen/distributions/size_frequency.h"

#include "checks.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

void CheckRange(const DiameterRange& range) {
    if (!(std::isfinite(range.min) && range.min >= 0.0 && range.max > range.min) ||
        std::isnan(range.max)) {
        throw std::invalid_argument("Diameter range must satisfy 0 <= min < max");
    }
}

void CheckInterval(double dMin, double dMax) {
    if (!(detail::PositiveFinite(dMin) && dMax > dMin) || std::isnan(dMax)) {
        throw std::invalid_argument("Diameter interval must satisfy 0 < dMin < dMax");
    }
}

}  // namespace

SizeFrequencyDistribution::SizeFrequencyDistribution(SfdKind kind, DiameterRange range)
    : kind_(kind), range_(range) {
    CheckRange(range_);
}

void SizeFrequencyDistribution::CheckDiameter(double diameter) const {
    if (!detail::PositiveFinite(diameter)) {
        throw std::domain_error("Diameter must be positive and finite");
    }
    if (!extrapolate_ && !range_.Contains(diameter)) {
        throw std::domain_error("Diameter " + std::to_string(diameter) +
                                " m is outside the distribution's valid range");
    }
}

double SizeFrequencyDistribution::Cumulative(double diameter) const {
    CheckDiameter(diameter);
    return CumulativeAt(diameter);
}

double SizeFrequencyDistribution::Incremental(double diameter) const {
    CheckDiameter(diameter);
    return IncrementalAt(diameter);
}

double SizeFrequencyDistribution::IncrementalAt(double diameter) const {
    constexpr double kStep = 1e-5;
    const double low = diameter * std::exp(-kStep);
    const double high = diameter * std::exp(kStep);
    return (CumulativeAt(low) - CumulativeAt(high)) / (high - low);
}

double SizeFrequencyDistribution::InvertCumulative(double target, double dMin, double dMax) const {
    double low = std::log(dMin);
    double high = std::isinf(dMax) ? low + 1.0 : std::log(dMax);
    while (std::isinf(dMax) && CumulativeAt(std::exp(high)) > target) {
        high += 1.0;
    }
    for (int i = 0; i < 200 && high - low > 1e-15 * std::fmax(1.0, std::fabs(high)); ++i) {
        const double middle = 0.5 * (low + high);
        if (CumulativeAt(std::exp(middle)) > target) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return std::exp(0.5 * (low + high));
}

double SizeFrequencyDistribution::ExpectedCount(double area, double dMin, double dMax) const {
    CheckInterval(dMin, dMax);
    if (!(std::isfinite(area) && area >= 0.0)) {
        throw std::invalid_argument("Area must be finite and not negative");
    }
    double upper = 0.0;
    if (std::isinf(dMax)) {
        if (!extrapolate_ && !std::isinf(range_.max)) {
            throw std::domain_error("An infinite dMax needs an unbounded valid range");
        }
    } else {
        upper = Cumulative(dMax);
    }
    return area * (Cumulative(dMin) - upper);
}

double SizeFrequencyDistribution::SampleDiameter(RandomStream& random, double dMin,
                                                 double dMax) const {
    const double count = ExpectedCount(1.0, dMin, dMax);
    if (!(count > 0.0)) {
        throw std::invalid_argument("The distribution has no mass in the diameter interval");
    }
    const double top = Cumulative(dMin);
    const double target = top - random.NextDouble() * count;
    return std::clamp(InvertCumulative(target, dMin, dMax), dMin, dMax);
}

}  // namespace terraingen
