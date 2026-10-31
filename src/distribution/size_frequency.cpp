// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/distribution/size_frequency.h"

#include "util/checks.h"
#include "util/numerics.h"

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

void CheckInterval(double minDiameter, double maxDiameter) {
    if (!(IsPositive(minDiameter) && maxDiameter > minDiameter) || std::isnan(maxDiameter)) {
        throw std::invalid_argument("Diameter interval must satisfy 0 < minDiameter < maxDiameter");
    }
}

/// The diameter in the bracket [`low`, `high`] of log diameters where N = `target` > 0, by Illinois
/// steps on log N; unset when they do not converge, with the bracket narrowed.
template <typename Cumulative>
std::optional<double> InvertBySecant(const Cumulative& cumulative, double target, double& low,
                                     double& high) {
    const double logTarget = std::log(target);
    const auto excess = [&](double u) { return std::log(cumulative(std::exp(u))) - logTarget; };
    IllinoisBracket bracket{low, high, excess(low), excess(high)};
    if (!(bracket.fa > 0.0)) {
        return std::exp(low);
    }
    if (!(bracket.fb < 0.0)) {
        return std::exp(high);
    }
    double u = low;
    for (int i = 0; i < 100; ++i) {
        const double previous = u;
        u = bracket.Next();
        const double g = excess(u);
        if (std::isnan(g)) {
            break;
        }
        if (g == 0.0 || std::fabs(u - previous) <= 1e-14 * std::fmax(1.0, std::fabs(u))) {
            return std::exp(u);
        }
        bracket.Update(u, g);
    }
    low = bracket.a;
    high = bracket.b;
    return std::nullopt;
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

double SizeFrequencyDistribution::InvertCumulative(double target, double minDiameter,
                                                   double maxDiameter) const {
    const auto cumulative = [this](double diameter) { return CumulativeAt(diameter); };
    double low = std::log(minDiameter);
    double high = std::isinf(maxDiameter) ? low + 1.0 : std::log(maxDiameter);
    while (std::isinf(maxDiameter) && CumulativeAt(std::exp(high)) > target) {
        high += 1.0;
    }
    if (target > 0.0) {
        if (const std::optional<double> found = InvertBySecant(cumulative, target, low, high)) {
            return *found;
        }
    }
    const double tolerance = 1e-15 * std::fmax(1.0, std::fabs(high));
    const BisectionInterval found = Bisect(
        low, high, 200, tolerance, [&](double u) { return !(CumulativeAt(std::exp(u)) > target); });
    return std::exp(0.5 * (found.low + found.high));
}

double SizeFrequencyDistribution::ExpectedCount(double area, double minDiameter,
                                                double maxDiameter) const {
    CheckInterval(minDiameter, maxDiameter);
    RequireNonNegative(area, "Area");
    double upper = 0.0;
    if (std::isinf(maxDiameter)) {
        if (!extrapolate_ && !std::isinf(range_.max)) {
            throw std::domain_error("An infinite maxDiameter needs an unbounded valid range");
        }
    } else {
        upper = Cumulative(maxDiameter);
    }
    return area * (Cumulative(minDiameter) - upper);
}

double SizeFrequencyDistribution::SampleDiameter(RandomStream& random, double minDiameter,
                                                 double maxDiameter) const {
    const double count = ExpectedCount(1.0, minDiameter, maxDiameter);
    if (!(count > 0.0)) {
        throw std::invalid_argument("The distribution has no mass in the diameter interval");
    }
    const double top = Cumulative(minDiameter);
    const double target = top - random.NextDouble() * count;
    return std::clamp(InvertCumulative(target, minDiameter, maxDiameter), minDiameter, maxDiameter);
}

}  // namespace terraingen
