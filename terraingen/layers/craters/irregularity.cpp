#include "terraingen/layers/craters/irregularity.h"

#include "terraingen/random/stream.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace terraingen {

namespace {

constexpr std::uint32_t kIrregularityStream = 0x49525247u;

/// sum over k of k^-slope squared, from `first` to `last`.
double PowerSum(int first, int last, double slope) {
    double sum = 0.0;
    for (int k = first; k <= last; ++k) {
        sum += std::pow(static_cast<double>(k), -2.0 * slope);
    }
    return sum;
}

}  // namespace

double ValueDistribution::Sample(double u1, double u2) const {
    double value = a;
    const auto normal = [&] {
        return std::sqrt(-2.0 * std::log1p(-u1)) * std::cos(2.0 * std::numbers::pi * u2);
    };
    switch (kind) {
    case Kind::Constant:
        break;
    case Kind::Uniform:
        value = a + (b - a) * u1;
        break;
    case Kind::Normal:
        value = a + b * normal();
        break;
    case Kind::LogNormal:
        value = a * std::exp(b * normal());
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

void ValidateCraterIrregularity(const CraterIrregularity& irregularity) {
    ValidateValueDistribution(irregularity.roughness);
    ValidateValueDistribution(irregularity.spectralSlope);
    ValidateValueDistribution(irregularity.ellipticity);
    for (const double share : {irregularity.roughShare, irregularity.elongatedShare}) {
        if (!(share >= 0.0 && share <= 1.0)) {
            throw std::invalid_argument("Crater irregularity shares must be in [0, 1]");
        }
    }
    if (!(irregularity.minDiameter >= 0.0 && irregularity.maxDiameter >= irregularity.minDiameter &&
          irregularity.minAgeGyr >= 0.0 && irregularity.maxAgeGyr >= irregularity.minAgeGyr)) {
        throw std::invalid_argument(
            "Crater irregularity size and age ranges must satisfy 0 <= min <= max");
    }
    if (!(irregularity.minHarmonic >= 1 && irregularity.minHarmonic <= irregularity.maxHarmonic &&
          irregularity.maxHarmonic <= 64)) {
        throw std::invalid_argument("Crater harmonics must satisfy 1 <= min <= max <= 64");
    }
    const int harmonics = irregularity.maxHarmonic - irregularity.minHarmonic + 1;
    if (!(irregularity.roughness.Lowest() >= 0.0 &&
          irregularity.roughness.Highest() * std::sqrt(2.0 * harmonics) < 0.5)) {
        throw std::invalid_argument(
            "Crater roughness must not be negative, and at most 0.5 / sqrt(2 n) radii for n "
            "harmonics, so rims cannot fold");
    }
    if (!(irregularity.ellipticity.Lowest() >= 1.0)) {
        throw std::invalid_argument("Crater ellipticity must be at least 1");
    }
    if ((irregularity.heading && !std::isfinite(*irregularity.heading)) ||
        !(std::isfinite(irregularity.headingSpread) && irregularity.headingSpread >= 0.0)) {
        throw std::invalid_argument("Crater heading and spread must be finite, the spread >= 0");
    }
}

double CraterOutline::Scale(double dx, double dy) const {
    if (Round()) {
        return 1.0;
    }
    const double theta = std::atan2(dy, dx);
    double scale = 1.0;
    if (ellipticity != 1.0) {
        const double along = std::sqrt(ellipticity);
        const double across = 1.0 / along;
        const double c = std::cos(theta - heading) / along;
        const double s = std::sin(theta - heading) / across;
        scale = 1.0 / std::sqrt(c * c + s * s);
    }
    double wobble = 1.0;
    for (std::size_t n = 0; n < amplitudes.size(); ++n) {
        const double k = static_cast<double>(firstHarmonic) + static_cast<double>(n);
        wobble += amplitudes[n] * std::cos(k * theta + phases[n]);
    }
    return scale * wobble;
}

double CraterOutline::MaxScale() const {
    double sum = 1.0;
    for (const double amplitude : amplitudes) {
        sum += std::fabs(amplitude);
    }
    return std::sqrt(ellipticity) * sum;
}

CraterOutline OutlineOf(const Crater& crater, const CraterIrregularity& irregularity) {
    CraterOutline outline;
    if (!irregularity.enabled || (crater.manual && !irregularity.manual) ||
        crater.diameter < irregularity.minDiameter || crater.diameter > irregularity.maxDiameter ||
        crater.ageGyr < irregularity.minAgeGyr || crater.ageGyr > irregularity.maxAgeGyr) {
        return outline;
    }
    RandomStream random(irregularity.seed, kIrregularityStream,
                        static_cast<std::uint32_t>(crater.id),
                        static_cast<std::uint32_t>(crater.id >> 32));
    const double rough = random.NextDouble();
    const double elongated = random.NextDouble();
    const double r1 = random.NextDouble();
    const double r2 = random.NextDouble();
    const double s1 = random.NextDouble();
    const double s2 = random.NextDouble();
    const double e1 = random.NextDouble();
    const double e2 = random.NextDouble();
    const double h = random.NextDouble();
    if (elongated < irregularity.elongatedShare) {
        outline.ellipticity = irregularity.ellipticity.Sample(e1, e2);
        outline.heading = irregularity.heading
                              ? *irregularity.heading + irregularity.headingSpread * (2.0 * h - 1.0)
                              : std::numbers::pi * h;
    }
    if (rough < irregularity.roughShare) {
        const double roughness = irregularity.roughness.Sample(r1, r2);
        const double slope = irregularity.spectralSlope.Sample(s1, s2);
        const double norm = roughness * std::sqrt(2.0 / PowerSum(irregularity.minHarmonic,
                                                                 irregularity.maxHarmonic, slope));
        outline.firstHarmonic = irregularity.minHarmonic;
        if (roughness > 0.0) {
            for (int k = irregularity.minHarmonic; k <= irregularity.maxHarmonic; ++k) {
                outline.amplitudes.push_back(norm * std::pow(static_cast<double>(k), -slope));
                outline.phases.push_back(2.0 * std::numbers::pi * random.NextDouble());
            }
        }
    }
    return outline;
}

double MaxOutlineScale(const CraterIrregularity& irregularity) {
    if (!irregularity.enabled) {
        return 1.0;
    }
    const int harmonics = irregularity.maxHarmonic - irregularity.minHarmonic + 1;
    const double rough = irregularity.roughShare > 0.0
                             ? 1.0 + irregularity.roughness.Highest() * std::sqrt(2.0 * harmonics)
                             : 1.0;
    const double elongated =
        irregularity.elongatedShare > 0.0 ? std::sqrt(irregularity.ellipticity.Highest()) : 1.0;
    return rough * elongated;
}

}  // namespace terraingen
