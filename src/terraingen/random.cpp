#include "terraingen/core/random.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace terraingen {

RandomStream::RandomStream(std::uint32_t seed, std::uint32_t stream, std::uint32_t a,
                           std::uint32_t b, std::uint32_t c)
    : key_{seed, stream}, counter_{a, b, c, 0u} {}

std::uint32_t RandomStream::NextU32() {
    if (used_ == 4) {
        block_ = Philox4x32(counter_, key_);
        ++counter_.w;
        used_ = 0;
    }
    const std::uint32_t words[4] = {block_.x, block_.y, block_.z, block_.w};
    return words[used_++];
}

float RandomStream::NextFloat() {
    return static_cast<float>(NextU32() >> 8u) * 0x1p-24f;
}

double RandomStream::NextDouble() {
    const std::uint64_t high = NextU32() >> 5u;
    const std::uint64_t low = NextU32() >> 6u;
    return static_cast<double>((high << 26u) | low) * 0x1p-53;
}

double RandomStream::NextDoubleOpenBelow() {
    return 1.0 - NextDouble();
}

namespace {

std::uint64_t PoissonMultiplication(RandomStream& random, double mean) {
    const double limit = std::exp(-mean);
    std::uint64_t count = 0;
    double product = random.NextDouble();
    while (product > limit) {
        ++count;
        product *= random.NextDouble();
    }
    return count;
}

std::uint64_t PoissonPtrs(RandomStream& random, double mean) {
    const double sqrtMean = std::sqrt(mean);
    const double logMean = std::log(mean);
    const double b = 0.931 + 2.53 * sqrtMean;
    const double a = -0.059 + 0.02483 * b;
    const double inverseAlpha = 1.1239 + 1.1328 / (b - 3.4);
    const double vr = 0.9277 - 3.6224 / (b - 2.0);
    while (true) {
        const double u = random.NextDouble() - 0.5;
        const double v = random.NextDouble();
        const double us = 0.5 - std::fabs(u);
        const double k = std::floor((2.0 * a / us + b) * u + mean + 0.43);
        if (us >= 0.07 && v <= vr) {
            return static_cast<std::uint64_t>(k);
        }
        if (k < 0.0 || (us < 0.013 && v > us)) {
            continue;
        }
        if (std::log(v) + std::log(inverseAlpha) - std::log(a / (us * us) + b) <=
            -mean + k * logMean - std::lgamma(k + 1.0)) {
            return static_cast<std::uint64_t>(k);
        }
    }
}

}  // namespace

std::uint64_t SamplePoisson(RandomStream& random, double mean) {
    if (!(std::isfinite(mean) && mean >= 0.0 && mean <= 1e9)) {
        throw std::invalid_argument("Poisson mean must be finite and in [0, 1e9]");
    }
    if (mean == 0.0) {
        return 0;
    }
    return mean < 10.0 ? PoissonMultiplication(random, mean) : PoissonPtrs(random, mean);
}

double SampleTruncatedPowerLaw(RandomStream& random, double min, double max, double exponent) {
    if (!(std::isfinite(min) && min > 0.0 && max > min && !std::isnan(max))) {
        throw std::invalid_argument("Power law bounds must satisfy 0 < min < max");
    }
    if (!(std::isfinite(exponent) && exponent > 0.0)) {
        throw std::invalid_argument("Power law exponent must be positive and finite");
    }
    const double low = std::pow(min, -exponent);
    const double high = std::isinf(max) ? 0.0 : std::pow(max, -exponent);
    const double u = random.NextDouble();
    const double x = std::pow(low - u * (low - high), -1.0 / exponent);
    return std::clamp(x, min, max);
}

double SampleExponential(RandomStream& random, double rate) {
    if (!(std::isfinite(rate) && rate > 0.0)) {
        throw std::invalid_argument("Exponential rate must be positive and finite");
    }
    return -std::log(random.NextDoubleOpenBelow()) / rate;
}

double SampleNormal(RandomStream& random, double mean, double sigma) {
    if (!std::isfinite(mean) || !(std::isfinite(sigma) && sigma >= 0.0)) {
        throw std::invalid_argument("Normal mean must be finite and sigma finite and not negative");
    }
    constexpr double kTwoPi = 6.283185307179586477;
    const double radius = std::sqrt(-2.0 * std::log(random.NextDoubleOpenBelow()));
    return mean + sigma * radius * std::cos(kTwoPi * random.NextDouble());
}

TabulatedDistribution::TabulatedDistribution(std::vector<double> values,
                                             std::vector<double> cumulative)
    : values_(std::move(values)), cumulative_(std::move(cumulative)) {
    if (values_.size() < 2 || values_.size() != cumulative_.size()) {
        throw std::invalid_argument(
            "A tabulated distribution needs at least two values and one CDF value per value");
    }
    for (std::size_t i = 0; i < values_.size(); ++i) {
        if (!std::isfinite(values_[i]) || !std::isfinite(cumulative_[i])) {
            throw std::invalid_argument("Tabulated values and CDF must be finite");
        }
        if (i > 0 && (values_[i] <= values_[i - 1] || cumulative_[i] < cumulative_[i - 1])) {
            throw std::invalid_argument(
                "Tabulated values must increase strictly and the CDF must not decrease");
        }
    }
    if (cumulative_.front() != 0.0 || cumulative_.back() != 1.0) {
        throw std::invalid_argument("Tabulated CDF must start at 0 and end at 1");
    }
}

double TabulatedDistribution::Cdf(double x) const {
    if (x <= values_.front()) {
        return 0.0;
    }
    if (x >= values_.back()) {
        return 1.0;
    }
    const auto upper = std::upper_bound(values_.begin(), values_.end(), x);
    const auto i = static_cast<std::size_t>(std::distance(values_.begin(), upper)) - 1;
    const double t = (x - values_[i]) / (values_[i + 1] - values_[i]);
    return cumulative_[i] + t * (cumulative_[i + 1] - cumulative_[i]);
}

double TabulatedDistribution::Sample(RandomStream& random) const {
    const double u = random.NextDouble();
    const auto upper = std::upper_bound(cumulative_.begin(), cumulative_.end(), u);
    const auto i = static_cast<std::size_t>(std::distance(cumulative_.begin(), upper)) - 1;
    const double t = (u - cumulative_[i]) / (cumulative_[i + 1] - cumulative_[i]);
    return values_[i] + t * (values_[i + 1] - values_[i]);
}

}  // namespace terraingen
