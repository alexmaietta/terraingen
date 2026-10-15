#include "terraingen/random/value_distribution.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace terraingen {

double StandardNormal(double u1, double u2) {
    return std::sqrt(-2.0 * std::log1p(-u1)) * std::cos(2.0 * std::numbers::pi * u2);
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
}

}  // namespace terraingen
