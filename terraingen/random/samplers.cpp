#include "terraingen/random/samplers.h"

#include "terraingen/util/bits.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

namespace {

/// Poisson count by multiplying uniforms until the product falls below exp(-`mean`); for small
/// means.
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

/// Poisson count by Hormann's transformed rejection with squeeze (PTRS); for means of 10 or more.
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

double StandardNormal(double u1, double u2) {
    return std::sqrt(-2.0 * std::log1p(-u1)) * std::cos(2.0 * kPi * u2);
}

double StandardNormal(RandomStream& random) {
    const double u1 = random.NextDouble();
    const double u2 = random.NextDouble();
    return StandardNormal(u1, u2);
}

}  // namespace terraingen
