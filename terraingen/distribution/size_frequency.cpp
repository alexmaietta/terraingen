// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/distribution/size_frequency.h"

#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <optional>
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
    if (!(IsPositive(dMin) && dMax > dMin) || std::isnan(dMax)) {
        throw std::invalid_argument("Diameter interval must satisfy 0 < dMin < dMax");
    }
}

/// Narrows the bracket [`low`, `high`] of log diameters around N = `target` > 0 by the Illinois
/// variant of regula falsi on log N; the diameter when it converges or the target lies outside the
/// bracket, otherwise nothing.
template <typename Cumulative>
std::optional<double> InvertBySecant(const Cumulative& cumulative, double target, double& low,
                                     double& high) {
    const double logTarget = std::log(target);
    const auto excess = [&](double u) { return std::log(cumulative(std::exp(u))) - logTarget; };
    double gLow = excess(low);
    double gHigh = excess(high);
    if (!(gLow > 0.0)) {
        return std::exp(low);
    }
    if (!(gHigh < 0.0)) {
        return std::exp(high);
    }
    int side = 0;
    double u = low;
    for (int i = 0; i < 100; ++i) {
        const double previous = u;
        u = (low * gHigh - high * gLow) / (gHigh - gLow);
        if (!(u > low && u < high)) {
            u = 0.5 * (low + high);
        }
        const double g = excess(u);
        if (std::isnan(g)) {
            break;
        }
        if (g == 0.0 || std::fabs(u - previous) <= 1e-14 * std::fmax(1.0, std::fabs(u))) {
            return std::exp(u);
        }
        if (g > 0.0) {
            low = u;
            gLow = g;
            if (side == 1) {
                gHigh *= 0.5;
            }
            side = 1;
        } else {
            high = u;
            gHigh = g;
            if (side == -1) {
                gLow *= 0.5;
            }
            side = -1;
        }
    }
    return std::nullopt;
}

/// The diameter with N = `target` by bisection of the bracket [`low`, `high`] of log diameters.
template <typename Cumulative>
double InvertByBisection(const Cumulative& cumulative, double target, double low, double high) {
    for (int i = 0; i < 200 && high - low > 1e-15 * std::fmax(1.0, std::fabs(high)); ++i) {
        const double middle = 0.5 * (low + high);
        if (cumulative(std::exp(middle)) > target) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return std::exp(0.5 * (low + high));
}

}  // namespace

SizeFrequencyDistribution::SizeFrequencyDistribution(SfdKind kind, DiameterRange range)
    : kind_(kind), range_(range) {
    CheckRange(range_);
}

void SizeFrequencyDistribution::CheckDiameter(double diameter) const {
    if (!IsPositive(diameter)) {
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
    const auto cumulative = [this](double diameter) { return CumulativeAt(diameter); };
    double low = std::log(dMin);
    double high = std::isinf(dMax) ? low + 1.0 : std::log(dMax);
    while (std::isinf(dMax) && CumulativeAt(std::exp(high)) > target) {
        high += 1.0;
    }
    if (target > 0.0) {
        if (const std::optional<double> found = InvertBySecant(cumulative, target, low, high)) {
            return *found;
        }
    }
    return InvertByBisection(cumulative, target, low, high);
}

double SizeFrequencyDistribution::ExpectedCount(double area, double dMin, double dMax) const {
    CheckInterval(dMin, dMax);
    RequireNonNegative(area, "Area");
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
