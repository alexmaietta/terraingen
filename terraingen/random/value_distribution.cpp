// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/random/value_distribution.h"

#include "terraingen/random/samplers.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <stdexcept>

namespace terraingen {

namespace {

/// The value at `u` in [0, 1) of the inverse CDF of piecewise-uniform bins; bins without weight
/// are never drawn.
double SamplePiecewise(const std::vector<double>& edges, const std::vector<double>& weights,
                       double u) {
    double target = u * std::accumulate(weights.begin(), weights.end(), 0.0);
    std::size_t bin = 0;
    while (bin + 1 < weights.size() && !(target < weights[bin])) {
        target -= weights[bin];
        ++bin;
    }
    while (weights[bin] == 0.0 && bin > 0) {
        --bin;
        target = weights[bin];
    }
    const double share = std::clamp(target / weights[bin], 0.0, 1.0);
    return edges[bin] + (edges[bin + 1] - edges[bin]) * share;
}

void CheckPiecewise(const ValueDistribution& d) {
    if (d.edges.size() < 2 || d.weights.size() + 1 != d.edges.size()) {
        throw std::invalid_argument(
            "A piecewise distribution needs at least two edges and one weight per bin");
    }
    for (std::size_t i = 0; i + 1 < d.edges.size(); ++i) {
        if (!(std::isfinite(d.edges[i]) && std::isfinite(d.edges[i + 1]) &&
              d.edges[i] < d.edges[i + 1])) {
            throw std::invalid_argument(
                "A piecewise distribution needs finite, strictly increasing edges");
        }
    }
    double total = 0.0;
    for (const double weight : d.weights) {
        RequireNonNegative(weight, "Piecewise weight");
        total += weight;
    }
    if (!IsPositive(total)) {
        throw std::invalid_argument("A piecewise distribution needs weights summing above zero");
    }
}

}  // namespace

double ValueDistribution::Draw(RandomStream& random) const {
    const double u1 = random.NextDouble();
    const double u2 = random.NextDouble();
    return Sample(u1, u2);
}

double ValueDistribution::Sample(double u1, double u2) const {
    double value = a;
    switch (kind) {
    case Kind::Constant:
        break;
    case Kind::Uniform:
        value = a + (b - a) * u1;
        break;
    case Kind::Normal:
        value = a + b * StandardNormal(u1, u2);
        break;
    case Kind::LogNormal:
        value = a * std::exp(b * StandardNormal(u1, u2));
        break;
    case Kind::Piecewise:
        value = SamplePiecewise(edges, weights, u1);
        break;
    }
    return std::clamp(value, min, max);
}

double ValueDistribution::Lowest() const {
    switch (kind) {
    case Kind::Constant:
    case Kind::Uniform:
        return std::clamp(a, min, max);
    case Kind::Normal:
        return min;
    case Kind::LogNormal:
        return std::fmax(0.0, min);
    case Kind::Piecewise:
        return std::clamp(edges.front(), min, max);
    }
    return min;
}

double ValueDistribution::Highest() const {
    switch (kind) {
    case Kind::Constant:
        return std::clamp(a, min, max);
    case Kind::Uniform:
        return std::clamp(b, min, max);
    case Kind::Normal:
    case Kind::LogNormal:
        return max;
    case Kind::Piecewise:
        return std::clamp(edges.back(), min, max);
    }
    return max;
}

void ValidateValueDistribution(const ValueDistribution& d) {
    if (!(std::isfinite(d.a) && std::isfinite(d.b)) || std::isnan(d.min) || std::isnan(d.max) ||
        d.min > d.max) {
        throw std::invalid_argument("Distribution parameters must be finite with min <= max");
    }
    if (d.kind == ValueDistribution::Kind::Uniform && d.b < d.a) {
        throw std::invalid_argument("A uniform distribution needs b >= a");
    }
    if ((d.kind == ValueDistribution::Kind::Normal ||
         d.kind == ValueDistribution::Kind::LogNormal) &&
        (d.b < 0.0 || !std::isfinite(d.max))) {
        throw std::invalid_argument(
            "Normal and log-normal distributions need a non-negative spread and a finite max");
    }
    if (d.kind == ValueDistribution::Kind::LogNormal && !(d.a > 0.0)) {
        throw std::invalid_argument("A log-normal distribution needs a positive median");
    }
    if (d.kind == ValueDistribution::Kind::Piecewise) {
        CheckPiecewise(d);
    } else if (!(d.edges.empty() && d.weights.empty())) {
        throw std::invalid_argument("Only a piecewise distribution takes edges and weights");
    }
}

}  // namespace terraingen
