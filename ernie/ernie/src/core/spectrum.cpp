#include "ernie/core/spectrum.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace ernie {

namespace {

/// Piecewise Gaussian of Wyman et al.: width `below` left of `mean` and `above` right of it.
double Lobe(double wavelength, double mean, double below, double above) {
    const double t = (wavelength - mean) / (wavelength < mean ? below : above);
    return std::exp(-0.5 * t * t);
}

/// Sub-samples per band when averaging analytic functions, enough for 1e-4 relative accuracy
/// with bands up to 100 nm.
constexpr int kSubSamples = 32;

template <typename Function> auto BandAverage(const SpectralBands& bands, int band, Function f) {
    using Result = decltype(f(0.0));
    Result sum{};
    for (int k = 0; k < kSubSamples; ++k) {
        sum += f(bands.Lower(band) + (k + 0.5) * bands.Width() / kSubSamples);
    }
    return sum / static_cast<double>(kSubSamples);
}

}  // namespace

void SpectralBands::Check() const {
    if (!(minNm > 0.0) || !(maxNm > minNm) || count < 1 || count > kMaxBands) {
        throw std::invalid_argument("A spectral range needs 0 < min < max and 1 to " +
                                    std::to_string(kMaxBands) + " bands");
    }
}

Spectrum::Spectrum(std::vector<double> wavelengthsNm, std::vector<double> values)
    : wavelengths_(std::move(wavelengthsNm)), values_(std::move(values)) {
    if (wavelengths_.size() != values_.size()) {
        throw std::invalid_argument("A spectrum needs one value per wavelength");
    }
    for (std::size_t i = 1; i < wavelengths_.size(); ++i) {
        if (!(wavelengths_[i] > wavelengths_[i - 1])) {
            throw std::invalid_argument("Spectrum wavelengths must be strictly increasing");
        }
    }
}

Spectrum Spectrum::Constant(double value) {
    return Spectrum({1.0}, {value});
}

Spectrum Spectrum::FromPairs(std::span<const float> pairs) {
    if (pairs.size() % 2 != 0) {
        throw std::invalid_argument("A spectrum needs (wavelength, value) pairs");
    }
    std::vector<double> wavelengths;
    std::vector<double> values;
    for (std::size_t i = 0; i < pairs.size(); i += 2) {
        wavelengths.push_back(pairs[i]);
        values.push_back(pairs[i + 1]);
    }
    return Spectrum(std::move(wavelengths), std::move(values));
}

double Spectrum::operator()(double wavelengthNm) const {
    if (wavelengths_.empty()) {
        return 0.0;
    }
    if (wavelengthNm <= wavelengths_.front()) {
        return values_.front();
    }
    if (wavelengthNm >= wavelengths_.back()) {
        return values_.back();
    }
    const auto upper = std::upper_bound(wavelengths_.begin(), wavelengths_.end(), wavelengthNm);
    const auto i = static_cast<std::size_t>(upper - wavelengths_.begin());
    const double t = (wavelengthNm - wavelengths_[i - 1]) / (wavelengths_[i] - wavelengths_[i - 1]);
    return values_[i - 1] + t * (values_[i] - values_[i - 1]);
}

double Spectrum::Integral(double lowNm, double highNm) const {
    std::vector<double> knots{lowNm, highNm};
    for (const double w : wavelengths_) {
        if (w > lowNm && w < highNm) {
            knots.push_back(w);
        }
    }
    std::sort(knots.begin(), knots.end());
    double sum = 0.0;
    for (std::size_t i = 1; i < knots.size(); ++i) {
        sum += 0.5 * ((*this)(knots[i - 1]) + (*this)(knots[i])) * (knots[i] - knots[i - 1]);
    }
    return sum;
}

double Spectrum::Average(double lowNm, double highNm) const {
    if (!(highNm > lowNm)) {
        return (*this)(lowNm);
    }
    return Integral(lowNm, highNm) / (highNm - lowNm);
}

std::vector<float> Spectrum::Bin(const SpectralBands& bands) const {
    bands.Check();
    std::vector<float> binned(static_cast<std::size_t>(bands.count));
    for (int i = 0; i < bands.count; ++i) {
        binned[static_cast<std::size_t>(i)] =
            static_cast<float>(Average(bands.Lower(i), bands.Lower(i) + bands.Width()));
    }
    return binned;
}

double PlanckRadiance(double wavelengthNm, double kelvin) {
    const double lambda = wavelengthNm * 1e-9;
    const double exponent = kPlanck * kSpeedOfLight / (lambda * kBoltzmann * kelvin);
    const double perMetre = 2.0 * kPlanck * kSpeedOfLight * kSpeedOfLight /
                            (std::pow(lambda, 5.0) * std::expm1(exponent));
    return perMetre * 1e-9;
}

std::vector<float> BlackbodyIrradiance(const SpectralBands& bands, double kelvin,
                                       double totalIrradiance) {
    bands.Check();
    if (!(kelvin > 0.0)) {
        throw std::invalid_argument("A black body needs a positive temperature");
    }
    const double totalRadiance = kStefanBoltzmann * std::pow(kelvin, 4.0) / M_PI;
    std::vector<float> irradiance(static_cast<std::size_t>(bands.count));
    for (int i = 0; i < bands.count; ++i) {
        const double radiance =
            BandAverage(bands, i, [kelvin](double w) { return PlanckRadiance(w, kelvin); });
        irradiance[static_cast<std::size_t>(i)] =
            static_cast<float>(totalIrradiance * radiance / totalRadiance);
    }
    return irradiance;
}

glm::dvec3 CieXyz(double w) {
    const double x = 1.056 * Lobe(w, 599.8, 37.9, 31.0) + 0.362 * Lobe(w, 442.0, 16.0, 26.7) -
                     0.065 * Lobe(w, 501.1, 20.4, 26.2);
    const double y = 0.821 * Lobe(w, 568.8, 46.9, 40.5) + 0.286 * Lobe(w, 530.9, 16.3, 31.1);
    const double z = 1.217 * Lobe(w, 437.0, 11.8, 36.0) + 0.681 * Lobe(w, 459.0, 26.0, 13.8);
    return {x, y, z};
}

std::vector<glm::dvec3> CieWeights(const SpectralBands& bands) {
    bands.Check();
    std::vector<glm::dvec3> weights(static_cast<std::size_t>(bands.count));
    for (int i = 0; i < bands.count; ++i) {
        weights[static_cast<std::size_t>(i)] = BandAverage(bands, i, CieXyz) * bands.Width();
    }
    return weights;
}

glm::dvec3 ToXyz(const SpectralBands& bands, std::span<const float> values) {
    const std::vector<glm::dvec3> weights = CieWeights(bands);
    if (values.size() != weights.size()) {
        throw std::invalid_argument("ToXyz needs one value per band");
    }
    glm::dvec3 xyz(0.0);
    for (std::size_t i = 0; i < values.size(); ++i) {
        xyz += static_cast<double>(values[i]) * weights[i];
    }
    return xyz;
}

glm::dvec3 XyzToLinearSrgb(glm::dvec3 xyz) {
    return {3.2404542 * xyz.x - 1.5371385 * xyz.y - 0.4985314 * xyz.z,
            -0.9692660 * xyz.x + 1.8760108 * xyz.y + 0.0415560 * xyz.z,
            0.0556434 * xyz.x - 0.2040259 * xyz.y + 1.0572252 * xyz.z};
}

namespace {

constexpr double kSigmoidMinNm = 360.0;
constexpr double kSigmoidMaxNm = 830.0;

double Sigmoid(double x) {
    return 0.5 + x / (2.0 * std::sqrt(1.0 + x * x));
}

/// Inverse of `Sigmoid` for values strictly inside (0, 1).
double InverseSigmoid(double y) {
    return (2.0 * y - 1.0) / (2.0 * std::sqrt(y * (1.0 - y)));
}

/// XYZ of a function of wavelength under the equal-energy illuminant, normalised by the
/// integral of y, integrated every 1 nm over 360-830 nm.
template <typename Function> glm::dvec3 EqualEnergyXyz(Function reflectance) {
    glm::dvec3 xyz(0.0);
    double y = 0.0;
    for (double w = kSigmoidMinNm; w <= kSigmoidMaxNm; w += 1.0) {
        const glm::dvec3 cmf = CieXyz(w);
        xyz += reflectance(w) * cmf;
        y += cmf.y;
    }
    return xyz / y;
}

glm::dvec3 Solve3(const glm::dmat3& a, glm::dvec3 b) {
    if (std::abs(glm::determinant(a)) < 1e-300) {
        return glm::dvec3(0.0);
    }
    return glm::inverse(a) * b;
}

}  // namespace

glm::dvec3 EqualEnergyXyzToLinearSrgb(glm::dvec3 xyz) {
    // Bradford cone response matrix, columns as GLM stores them (transposed rows).
    const glm::dmat3 bradford = glm::transpose(glm::dmat3(0.8951, 0.2664, -0.1614,  //
                                                          -0.7502, 1.7135, 0.0367,  //
                                                          0.0389, -0.0685, 1.0296));
    static const glm::dvec3 sourceWhite = EqualEnergyXyz([](double) { return 1.0; });
    const glm::dvec3 targetWhite(0.95047, 1.0, 1.08883);
    const glm::dvec3 source = bradford * sourceWhite;
    const glm::dvec3 target = bradford * targetWhite;
    const glm::dmat3 scale(glm::dvec3(target.x / source.x, 0.0, 0.0),
                           glm::dvec3(0.0, target.y / source.y, 0.0),
                           glm::dvec3(0.0, 0.0, target.z / source.z));
    return XyzToLinearSrgb(glm::inverse(bradford) * scale * bradford * xyz);
}

double SigmoidSpectrum::operator()(double wavelengthNm) const {
    const double t = (wavelengthNm - kSigmoidMinNm) / (kSigmoidMaxNm - kSigmoidMinNm);
    return Sigmoid((coefficients.x * t + coefficients.y) * t + coefficients.z);
}

Spectrum SigmoidSpectrum::Tabulate() const {
    std::vector<double> wavelengths;
    std::vector<double> values;
    for (double w = kSigmoidMinNm; w <= kSigmoidMaxNm; w += 5.0) {
        wavelengths.push_back(w);
        values.push_back((*this)(w));
    }
    return Spectrum(std::move(wavelengths), std::move(values));
}

glm::dvec3 ReflectanceToLinearSrgb(const Spectrum& reflectance) {
    return EqualEnergyXyzToLinearSrgb(EqualEnergyXyz([&](double w) { return reflectance(w); }));
}

SigmoidSpectrum RgbToSigmoid(glm::dvec3 rgb) {
    rgb = glm::clamp(rgb, glm::dvec3(0.0), glm::dvec3(1.0));
    const double mean = (rgb.r + rgb.g + rgb.b) / 3.0;
    const double flat = InverseSigmoid(std::clamp(mean, 1e-6, 1.0 - 1e-6));
    SigmoidSpectrum spectrum{{0.0, 0.0, flat}};
    if (rgb.r == rgb.g && rgb.g == rgb.b) {
        return spectrum;
    }
    const auto colour = [](const SigmoidSpectrum& candidate) {
        return EqualEnergyXyzToLinearSrgb(EqualEnergyXyz(candidate));
    };
    for (int iteration = 0; iteration < 40; ++iteration) {
        const glm::dvec3 residual = colour(spectrum) - rgb;
        if (glm::length(residual) < 1e-7) {
            break;
        }
        glm::dmat3 jacobian;
        for (int k = 0; k < 3; ++k) {
            SigmoidSpectrum nudged = spectrum;
            const double h = 1e-5 * std::max(1.0, std::abs(spectrum.coefficients[k]));
            nudged.coefficients[k] += h;
            jacobian[k] = (colour(nudged) - colour(spectrum)) / h;
        }
        const glm::dvec3 step = Solve3(jacobian, residual);
        double damping = 1.0;
        const double before = glm::length(residual);
        SigmoidSpectrum next = spectrum;
        for (int halving = 0; halving < 10; ++halving) {
            next.coefficients = spectrum.coefficients - damping * step;
            if (glm::length(colour(next) - rgb) < before) {
                break;
            }
            damping *= 0.5;
        }
        if (!(glm::length(colour(next) - rgb) < before)) {
            break;
        }
        spectrum = next;
    }
    return spectrum;
}

Spectrum RgbToSpectrum(glm::dvec3 rgb) {
    return RgbToSigmoid(rgb).Tabulate();
}

}  // namespace ernie
