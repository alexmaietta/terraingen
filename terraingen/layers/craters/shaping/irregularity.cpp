#include "terraingen/layers/craters/shaping/irregularity.h"

#include "terraingen/random/stream.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>

namespace terraingen {

namespace {

constexpr std::uint32_t kIrregularityStream = 0x49525247u;

constexpr std::size_t kTableSize = 1024;

/// A standard normal from the next two uniforms of `random`.
double Normal(RandomStream& random) {
    const double u1 = random.NextDouble();
    const double u2 = random.NextDouble();
    return StandardNormal(u1, u2);
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

/// The breakpoints of one crater's rim spectrum, from the fits at its diameter.
struct DuSpectrum {
    double slope;
    double logWavelength2;
    double logPower2;
    double logPower3;
    double logPower4;

    /// log10 power of harmonic `k`.
    double LogPower(std::size_t k) const {
        if (k == 1) {
            return logPower4;
        }
        if (k == 2) {
            return logPower3;
        }
        const double logPi = std::log10(std::numbers::pi);
        const double logWavelength = std::log10(2.0 * std::numbers::pi / static_cast<double>(k));
        if (logWavelength >= logWavelength2) {
            return logPower2 + (logWavelength - logWavelength2) / (logPi - logWavelength2) *
                                   (logPower3 - logPower2);
        }
        return logPower2 + slope * (logWavelength - logWavelength2);
    }
};

/// `table` scaled down so that no value exceeds `maxDeviation` in magnitude.
void LimitDeviation(std::vector<double>& table, double maxDeviation) {
    double largest = 0.0;
    for (const double value : table) {
        largest = std::fmax(largest, std::fabs(value));
    }
    if (largest > maxDeviation) {
        for (double& value : table) {
            value *= maxDeviation / largest;
        }
    }
}

std::vector<double> DuRimTable(double diameter, bool scatter, double maxDeviation,
                               RandomStream& random) {
    const double kilometres = std::clamp(diameter / 1000.0, 1.0, 140.0);
    const auto draw = [&] { return scatter ? Normal(random) : 0.0; };
    DuSpectrum fit{};
    fit.slope = kSlope12.At(kilometres, draw());
    fit.logWavelength2 = kLogWavelength2.At(kilometres, draw());
    fit.logPower2 = kLogPower2.At(kilometres, draw());
    fit.logPower3 = kLogPower3.At(kilometres, draw());
    fit.logPower4 = kLogPower4.At(kilometres, draw());
    std::vector<std::complex<double>> spectrum(kTableSize);
    for (std::size_t k = 1; k < kTableSize / 2; ++k) {
        const double noise = scatter ? kSpectrumSigma * Normal(random) : 0.0;
        const double amplitude =
            std::sqrt(std::pow(10.0, fit.LogPower(k) + noise) * 2.0 * std::numbers::pi) /
            kSpectrumVertices;
        const double phase = 2.0 * std::numbers::pi * random.NextDouble();
        spectrum[k] = std::complex<double>(0.0, -1.0) * std::polar(amplitude, phase);
    }
    Fft(spectrum, 1.0);
    std::vector<double> table(kTableSize);
    for (std::size_t n = 0; n < kTableSize; ++n) {
        table[n] = spectrum[n].real();
    }
    LimitDeviation(table, maxDeviation);
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

/// The uniforms an outline is drawn from, in the order drawn.
struct OutlineDraws {
    double rough, elongated, r1, r2, s1, s2, e1, e2, h;
};

OutlineDraws DrawOutline(RandomStream& random) {
    OutlineDraws draws{};
    for (double* draw : {&draws.rough, &draws.elongated, &draws.r1, &draws.r2, &draws.s1, &draws.s2,
                         &draws.e1, &draws.e2, &draws.h}) {
        *draw = random.NextDouble();
    }
    return draws;
}

/// Adds the power-law harmonics of `irregularity` to `outline`.
void AddHarmonics(CraterOutline& outline, const CraterIrregularity& irregularity,
                  const OutlineDraws& draws, RandomStream& random) {
    const double roughness = irregularity.roughness.Sample(draws.r1, draws.r2);
    const double slope = irregularity.spectralSlope.Sample(draws.s1, draws.s2);
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

}  // namespace

void ValidateCraterIrregularity(const CraterIrregularity& irregularity) {
    ValidateValueDistribution(irregularity.roughness);
    ValidateValueDistribution(irregularity.spectralSlope);
    ValidateValueDistribution(irregularity.ellipticity);
    for (const double share : {irregularity.roughShare, irregularity.elongatedShare}) {
        RequireUnitInterval(share, "Crater irregularity shares");
    }
    ValidateCraterSelection(irregularity.selection, "Crater irregularity");
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
    if (!irregularity.enabled || !irregularity.selection.Includes(crater)) {
        return outline;
    }
    const IdWords id = SplitId(crater.id);
    RandomStream random(irregularity.seed, kIrregularityStream, id.low, id.high);
    const OutlineDraws draws = DrawOutline(random);
    if (draws.elongated < irregularity.elongatedShare) {
        outline.ellipticity = irregularity.ellipticity.Sample(draws.e1, draws.e2);
        outline.heading =
            irregularity.heading
                ? *irregularity.heading + irregularity.headingSpread * (2.0 * draws.h - 1.0)
                : std::numbers::pi * draws.h;
    }
    if (draws.rough < irregularity.roughShare && irregularity.model == RimRoughnessModel::Du2024) {
        outline.table =
            DuRimTable(crater.diameter, irregularity.scatter, irregularity.maxDeviation, random);
    } else if (draws.rough < irregularity.roughShare) {
        AddHarmonics(outline, irregularity, draws, random);
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

double CraterProfileDistance(const Crater& crater, const CraterOutline& outline, double x, double y,
                             const Surface& surface) {
    const Vec2d offset = surface.Offset({crater.x, crater.y}, {x, y});
    const double distance = std::sqrt(offset.x * offset.x + offset.y * offset.y);
    return outline.Round() ? distance : distance / outline.Scale(offset.x, offset.y);
}

}  // namespace terraingen
