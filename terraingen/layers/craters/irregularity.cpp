#include "terraingen/layers/craters/irregularity.h"

#include "terraingen/random/stream.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>

namespace terraingen {

namespace {

constexpr std::uint32_t kIrregularityStream = 0x49525247u;

constexpr std::size_t kTableSize = 1024;

/// A standard normal from two uniforms (Box-Muller).
double Normal(RandomStream& random) {
    const double u1 = random.NextDouble();
    const double u2 = random.NextDouble();
    return std::sqrt(-2.0 * std::log1p(-u1)) * std::cos(2.0 * std::numbers::pi * u2);
}

/// In-place iterative radix-2 FFT of `values` (size a power of 2), with the sign of the exponent
/// `sign`.
void Fft(std::vector<std::complex<double>>& values, double sign) {
    const std::size_t n = values.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(values[i], values[j]);
        }
    }
    for (std::size_t length = 2; length <= n; length <<= 1) {
        const double angle = sign * 2.0 * std::numbers::pi / static_cast<double>(length);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (std::size_t start = 0; start < n; start += length) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < length / 2; ++k) {
                const std::complex<double> even = values[start + k];
                const std::complex<double> odd = values[start + k + length / 2] * w;
                values[start + k] = even + odd;
                values[start + k + length / 2] = even - odd;
                w *= step;
            }
        }
    }
}

/// y = a D + b in a crater's diameter D (km), with one line up to 20 km and another above, as Du et
/// al. (2024, Table 2) fit their breakpoints; and its 1-sigma misfit on each side.
struct PiecewiseFit {
    double a1, b1, sigma1, a2, b2, sigma2;

    double At(double kilometres, double z) const {
        return kilometres <= 20.0 ? a1 * kilometres + b1 + z * sigma1
                                  : a2 * kilometres + b2 + z * sigma2;
    }
};

/// The rim crest radial distance fits of Du et al. (2024, Table 2).
constexpr PiecewiseFit kSlope12{-0.022, 3.3, 0.54, -0.00019, 2.9, 0.22};
constexpr PiecewiseFit kLogWavelength2{0.010, -0.57, 0.27, -0.00035, -0.36, 0.27};
constexpr PiecewiseFit kLogPower2{0.043, 0.59, 0.55, 0.010, 1.2, 0.75};
constexpr PiecewiseFit kLogPower3{0.055, 2.3, 0.58, -0.0026, 3.5, 0.44};
constexpr PiecewiseFit kLogPower4{0.066, -1.1, 1.00, 0.0062, 0.059, 0.69};
constexpr double kSpectrumSigma = 0.55;
constexpr double kSpectrumVertices = 5000.0;

std::vector<double> DuRimTable(double diameter, bool scatter, double maxDeviation,
                               RandomStream& random) {
    const double kilometres = std::clamp(diameter / 1000.0, 1.0, 140.0);
    const auto draw = [&] { return scatter ? Normal(random) : 0.0; };
    const double slope = kSlope12.At(kilometres, draw());
    const double logWavelength2 = kLogWavelength2.At(kilometres, draw());
    const double logPower2 = kLogPower2.At(kilometres, draw());
    const double logPower3 = kLogPower3.At(kilometres, draw());
    const double logPower4 = kLogPower4.At(kilometres, draw());
    const double logPi = std::log10(std::numbers::pi);
    std::vector<std::complex<double>> spectrum(kTableSize);
    for (std::size_t k = 1; k < kTableSize / 2; ++k) {
        const double logWavelength = std::log10(2.0 * std::numbers::pi / static_cast<double>(k));
        double logPower = 0.0;
        if (k == 1) {
            logPower = logPower4;
        } else if (k == 2) {
            logPower = logPower3;
        } else if (logWavelength >= logWavelength2) {
            logPower = logPower2 + (logWavelength - logWavelength2) / (logPi - logWavelength2) *
                                       (logPower3 - logPower2);
        } else {
            logPower = logPower2 + slope * (logWavelength - logWavelength2);
        }
        const double noise = scatter ? kSpectrumSigma * Normal(random) : 0.0;
        const double amplitude =
            std::sqrt(std::pow(10.0, logPower + noise) * 2.0 * std::numbers::pi) /
            kSpectrumVertices;
        const double phase = 2.0 * std::numbers::pi * random.NextDouble();
        spectrum[k] = std::complex<double>(0.0, -1.0) * std::polar(amplitude, phase);
    }
    Fft(spectrum, 1.0);
    std::vector<double> table(kTableSize);
    double largest = 0.0;
    for (std::size_t n = 0; n < kTableSize; ++n) {
        table[n] = spectrum[n].real();
        largest = std::fmax(largest, std::fabs(table[n]));
    }
    if (largest > maxDeviation) {
        for (double& value : table) {
            value *= maxDeviation / largest;
        }
    }
    return table;
}

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
        RequireUnitInterval(share, "Crater irregularity shares");
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
    if (!(irregularity.maxDeviation > 0.0 && irregularity.maxDeviation <= 0.5)) {
        throw std::invalid_argument("Crater maxDeviation must be in (0, 0.5]");
    }
    if ((irregularity.heading && !std::isfinite(*irregularity.heading)) ||
        !IsNonNegative(irregularity.headingSpread)) {
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
    if (!table.empty()) {
        const double turns = theta / (2.0 * std::numbers::pi);
        const double position = (turns - std::floor(turns)) * static_cast<double>(table.size());
        const auto index = std::min(static_cast<std::size_t>(position), table.size() - 1);
        const double t = position - static_cast<double>(index);
        wobble += (1.0 - t) * table[index] + t * table[(index + 1) % table.size()];
    }
    for (std::size_t n = 0; n < amplitudes.size(); ++n) {
        const double k = static_cast<double>(firstHarmonic) + static_cast<double>(n);
        wobble += amplitudes[n] * std::cos(k * theta + phases[n]);
    }
    return scale * wobble;
}

double CraterOutline::MaxScale() const {
    double sum = 1.0;
    for (const double deviation : table) {
        sum = std::fmax(sum, 1.0 + deviation);
    }
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
    if (rough < irregularity.roughShare && irregularity.model == RimRoughnessModel::Du2024) {
        outline.table =
            DuRimTable(crater.diameter, irregularity.scatter, irregularity.maxDeviation, random);
    } else if (rough < irregularity.roughShare) {
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
    double rough = 1.0;
    if (irregularity.roughShare > 0.0) {
        rough = irregularity.model == RimRoughnessModel::Du2024
                    ? 1.0 + irregularity.maxDeviation
                    : 1.0 + irregularity.roughness.Highest() * std::sqrt(2.0 * harmonics);
    }
    const double elongated =
        irregularity.elongatedShare > 0.0 ? std::sqrt(irregularity.ellipticity.Highest()) : 1.0;
    return rough * elongated;
}

}  // namespace terraingen
