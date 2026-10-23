#include "terraingen/raster/spectrum.h"

#include "terraingen/raster/geo_transform.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/fft.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <stdexcept>
#include <utility>

namespace terraingen {

using detail::SpectrumBin;

namespace {

constexpr double kBinsPerOctave = 4.0;
constexpr std::size_t kMinWindow = 8;
constexpr std::size_t kMinFitBins = 3;
constexpr double kMinSlope = 2.0;
constexpr double kMaxSlope = 4.0;
constexpr double kFlatSlope = 3.0;

std::size_t FloorPowerOfTwo(std::size_t n) {
    std::size_t power = 1;
    while (power * 2 <= n) {
        power *= 2;
    }
    return power;
}

/// The integral of f^(1 - `slope`) over the octave from `nyquist` to twice it.
double OctaveIntegral(double slope, double nyquist) {
    const double exponent = 2.0 - slope;
    if (std::fabs(exponent) < 1e-9) {
        return std::log(2.0);
    }
    return std::pow(nyquist, exponent) * (std::pow(2.0, exponent) - 1.0) / exponent;
}

/// The Hann window over `n` samples at sample `i`.
double Hann(std::size_t i, std::size_t n) {
    return 0.5 -
           0.5 * std::cos(2.0 * kPi * (static_cast<double>(i) + 0.5) / static_cast<double>(n));
}

/// The centered power-of-2 window of a raster.
struct Window {
    std::size_t col0 = 0;
    std::size_t row0 = 0;
    std::size_t width = 0;
    std::size_t height = 0;
};

Window CenteredWindow(const Raster<float>& heights) {
    Window window;
    window.width = FloorPowerOfTwo(std::max<std::size_t>(heights.Width(), 1));
    window.height = FloorPowerOfTwo(std::max<std::size_t>(heights.Height(), 1));
    if (heights.Width() < kMinWindow || heights.Height() < kMinWindow) {
        throw std::invalid_argument("A power spectrum needs a raster at least 8 cells a side");
    }
    window.col0 = (heights.Width() - window.width) / 2;
    window.row0 = (heights.Height() - window.height) / 2;
    return window;
}

/// The least-squares plane z = c0 + c1 x + c2 y through the cells of `window` holding data, x and
/// y their offsets from the window's center in cells.
struct Plane {
    double c0 = 0.0;
    double c1 = 0.0;
    double c2 = 0.0;

    double At(double x, double y) const {
        return c0 + c1 * x + c2 * y;
    }
};

using Matrix3 = std::array<std::array<double, 3>, 3>;

double Determinant(const Matrix3& m) {
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
           m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

/// The offset of cell `i` of `n` from their center, in cells.
double Centered(std::size_t i, std::size_t n) {
    return static_cast<double>(i) - 0.5 * static_cast<double>(n - 1);
}

Plane FitPlane(const Raster<float>& heights, const Window& window) {
    Matrix3 normal{};
    std::array<double, 3> right{};
    for (std::size_t row = 0; row < window.height; ++row) {
        for (std::size_t col = 0; col < window.width; ++col) {
            const float value = heights(window.col0 + col, window.row0 + row);
            if (heights.IsMissing(value)) {
                continue;
            }
            const std::array<double, 3> basis{1.0, Centered(col, window.width),
                                              Centered(row, window.height)};
            for (std::size_t i = 0; i < 3; ++i) {
                for (std::size_t j = 0; j < 3; ++j) {
                    normal[i][j] += basis[i] * basis[j];
                }
                right[i] += basis[i] * static_cast<double>(value);
            }
        }
    }
    if (normal[0][0] == 0.0) {
        throw std::invalid_argument("A power spectrum needs cells with data");
    }
    const double det = Determinant(normal);
    if (!(std::fabs(det) > 1e-12 * normal[0][0] * normal[1][1] * normal[2][2])) {
        return {right[0] / normal[0][0], 0.0, 0.0};
    }
    std::array<double, 3> c{};
    for (std::size_t k = 0; k < 3; ++k) {
        Matrix3 replaced = normal;
        for (std::size_t i = 0; i < 3; ++i) {
            replaced[i][k] = right[i];
        }
        c[k] = Determinant(replaced) / det;
    }
    return {c[0], c[1], c[2]};
}

/// The window's heights minus their plane, zero without data, times the 2D Hann window; and the
/// window's summed squared weights.
std::pair<std::vector<std::complex<double>>, double> TaperedWindow(const Raster<float>& heights,
                                                                   const Window& window) {
    const Plane plane = FitPlane(heights, window);
    std::vector<std::complex<double>> values(window.width * window.height);
    double energyX = 0.0;
    double energyY = 0.0;
    for (std::size_t col = 0; col < window.width; ++col) {
        energyX += Hann(col, window.width) * Hann(col, window.width);
    }
    for (std::size_t row = 0; row < window.height; ++row) {
        energyY += Hann(row, window.height) * Hann(row, window.height);
        for (std::size_t col = 0; col < window.width; ++col) {
            const float value = heights(window.col0 + col, window.row0 + row);
            if (heights.IsMissing(value)) {
                continue;
            }
            const double trend =
                plane.At(Centered(col, window.width), Centered(row, window.height));
            values[row * window.width + col] = (static_cast<double>(value) - trend) *
                                               Hann(col, window.width) * Hann(row, window.height);
        }
    }
    return {std::move(values), energyX * energyY};
}

/// The signed frequency index of FFT bin `k` of `n`.
double SignedIndex(std::size_t k, std::size_t n) {
    return k <= n / 2 ? static_cast<double>(k) : static_cast<double>(k) - static_cast<double>(n);
}

/// The pixel size of a raster: the square root of a cell's area.
double CellSize(const Raster<float>& heights) {
    const GeoTransform& transform = heights.Transform();
    return std::sqrt(std::fabs(transform.pixelWidth * transform.pixelHeight));
}

/// A line fitted to log power over log frequency.
struct LogLine {
    double gradient = 0.0;
    double meanX = 0.0;  ///< Weighted mean log frequency.
    double meanY = 0.0;  ///< Weighted mean log power.
};

/// The least-squares line through log power over log frequency of `bins`, each weighted by its
/// frequency samples, as the variance of its mean falls with their number.
LogLine FitLogLine(const std::vector<SpectrumBin>& bins) {
    const double floor = std::numeric_limits<double>::min();
    double weights = 0.0;
    LogLine line;
    for (const SpectrumBin& bin : bins) {
        const auto weight = static_cast<double>(bin.count);
        weights += weight;
        line.meanX += weight * std::log(bin.frequency);
        line.meanY += weight * std::log(std::max(bin.power, floor));
    }
    line.meanX /= weights;
    line.meanY /= weights;
    double sxx = 0.0;
    double sxy = 0.0;
    for (const SpectrumBin& bin : bins) {
        const auto weight = static_cast<double>(bin.count);
        const double x = std::log(bin.frequency) - line.meanX;
        sxx += weight * x * x;
        sxy += weight * x * (std::log(std::max(bin.power, floor)) - line.meanY);
    }
    line.gradient = sxy / sxx;
    return line;
}

}  // namespace

double PowerLawSpectrum::Coefficient(double nyquist) const {
    return octaveRms * octaveRms / (2.0 * kPi * OctaveIntegral(slope, nyquist));
}

std::vector<detail::SpectrumBin> detail::RadialPowerSpectrum(const Raster<float>& heights) {
    const GeoTransform& transform = heights.Transform();
    if (transform.IsRotated()) {
        throw std::invalid_argument("A power spectrum needs an unrotated raster");
    }
    const Window window = CenteredWindow(heights);
    auto [values, energy] = TaperedWindow(heights, window);
    Fft2(values, window.width, window.height, -1.0);
    const double dx = std::fabs(transform.pixelWidth);
    const double dy = std::fabs(transform.pixelHeight);
    const double fundamental = 1.0 / std::max(static_cast<double>(window.width) * dx,
                                              static_cast<double>(window.height) * dy);
    const double nyquist = 0.5 / std::max(dx, dy);
    const auto bins = static_cast<std::size_t>(
        std::floor(kBinsPerOctave * std::log2(nyquist / fundamental) + 1e-9) + 1.0);
    std::vector<SpectrumBin> sums(bins);
    const double scale = dx * dy / energy;
    for (std::size_t v = 0; v < window.height; ++v) {
        const double fy = SignedIndex(v, window.height) / (static_cast<double>(window.height) * dy);
        for (std::size_t u = 0; u < window.width; ++u) {
            const double fx =
                SignedIndex(u, window.width) / (static_cast<double>(window.width) * dx);
            const double f = std::hypot(fx, fy);
            if (f == 0.0 || f > nyquist * (1.0 + 1e-12)) {
                continue;
            }
            const auto bin = static_cast<std::size_t>(
                std::clamp(std::floor(kBinsPerOctave * std::log2(f / fundamental) + 1e-9), 0.0,
                           static_cast<double>(bins - 1)));
            sums[bin].frequency += f;
            sums[bin].power += std::norm(values[v * window.width + u]) * scale;
            ++sums[bin].count;
        }
    }
    std::vector<SpectrumBin> spectrum;
    for (SpectrumBin& bin : sums) {
        if (bin.count > 0) {
            bin.frequency /= static_cast<double>(bin.count);
            bin.power /= static_cast<double>(bin.count);
            spectrum.push_back(bin);
        }
    }
    return spectrum;
}

void ValidateSpectrumFitBand(const SpectrumFitBand& band) {
    RequireFinite(band.minPixels, "Spectrum fit min pixels");
    if (band.minPixels < 2.0) {
        throw std::invalid_argument("Spectrum fit min pixels must be at least 2");
    }
    RequireFinite(band.maxPixels, "Spectrum fit max pixels");
    if (band.maxPixels != 0.0 && !(band.maxPixels > band.minPixels)) {
        throw std::invalid_argument("Spectrum fit max pixels must be 0 or above min pixels");
    }
}

PowerLawSpectrum EstimatePowerLawSpectrum(const Raster<float>& heights,
                                          const SpectrumFitBand& band) {
    ValidateSpectrumFitBand(band);
    const double pixel = CellSize(heights);
    const Window window = CenteredWindow(heights);
    const double maxPixels =
        band.maxPixels > 0.0 ? band.maxPixels
                             : 0.25 * static_cast<double>(std::min(window.width, window.height));
    const double low = 1.0 / (maxPixels * pixel) * (1.0 - 1e-9);
    const double high = 1.0 / (band.minPixels * pixel) * (1.0 + 1e-9);
    std::vector<SpectrumBin> bins;
    for (const SpectrumBin& bin : detail::RadialPowerSpectrum(heights)) {
        if (bin.frequency >= low && bin.frequency <= high) {
            bins.push_back(bin);
        }
    }
    if (bins.size() < kMinFitBins) {
        throw std::invalid_argument("A power-law fit needs at least 3 spectrum bins in its band");
    }
    PowerLawSpectrum fit;
    const bool relief = std::any_of(bins.begin(), bins.end(),
                                    [](const SpectrumBin& bin) { return bin.power > 0.0; });
    if (!relief) {
        fit.slope = kFlatSlope;
        return fit;
    }
    const LogLine line = FitLogLine(bins);
    fit.slope = std::clamp(-line.gradient, kMinSlope, kMaxSlope);
    const double logCoefficient = line.meanY + fit.slope * line.meanX;
    fit.octaveRms =
        std::sqrt(2.0 * kPi * std::exp(logCoefficient) * OctaveIntegral(fit.slope, 0.5 / pixel));
    return fit;
}

}  // namespace terraingen
