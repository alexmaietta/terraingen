#include "terraingen/layers/spectral_continuation.h"

#include "terraingen/random/stream.h"
#include "terraingen/random/streams.h"
#include "terraingen/raster/geo_transform.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/fft.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

constexpr double kBinsPerOctave = 4.0;
constexpr std::size_t kMinWindow = 8;
constexpr std::size_t kMinFitBins = 3;
constexpr double kMinSlope = 2.0;
constexpr double kMaxSlope = 4.0;
constexpr double kFlatSlope = 3.0;

/// The Gaussian envelope parameter a of a band's impulses over the band's lowest frequency: the
/// impulses' spectra are Gaussians of standard deviation a / (2 sqrt(pi)), about a seventh of it.
constexpr double kBandwidth = 0.5;
/// The envelope's value at the impulse radius, where it is cut and shifted to reach zero.
constexpr double kEnvelopeCut = 0.01;
constexpr int kImpulsesPerCell = 8;
constexpr int kMaxBands = 40;

/// Grid cells per side of the tiles a grid is evaluated in.
constexpr std::size_t kTileCells = 64;

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

/// The coefficient C of P(f) = C f^-`slope` that holds `octaveRms` in the octave above
/// `nyquist`.
double Coefficient(double slope, double octaveRms, double nyquist) {
    return octaveRms * octaveRms / (2.0 * kPi * OctaveIntegral(slope, nyquist));
}

/// The integral of the squared envelope over the plane, times a^2: the envelope is
/// (exp(-pi a^2 d^2) - c) / (1 - c) inside the radius where the Gaussian falls to c.
double EnvelopeEnergy() {
    const double c = kEnvelopeCut;
    const double u = -std::log(c);
    return ((1.0 - c * c) / 2.0 - 2.0 * c * (1.0 - c) + c * c * u) / ((1.0 - c) * (1.0 - c));
}

/// The Hann window over `n` samples at sample `i`.
double Hann(std::size_t i, std::size_t n) {
    return 0.5 -
           0.5 * std::cos(2.0 * kPi * (static_cast<double>(i) + 0.5) / static_cast<double>(n));
}

/// The centerd power-of-2 window of a raster.
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

std::vector<SpectrumBin> RadialPowerSpectrum(const Raster<float>& heights) {
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
    for (const SpectrumBin& bin : RadialPowerSpectrum(heights)) {
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

void ValidateSpectralContinuationOptions(const SpectralContinuationOptions& options) {
    RequirePositive(options.pixelSize, "Spectral continuation pixel size");
    RequireFinite(options.slope, "Spectral continuation slope");
    if (options.slope < kMinSlope || options.slope > kMaxSlope) {
        throw std::invalid_argument("Spectral continuation slope must be in [2, 4]");
    }
    RequireNonNegative(options.octaveRms, "Spectral continuation octave RMS");
    RequirePositive(options.minWavelength, "Spectral continuation min wavelength");
    RequirePositive(options.taperOctaves, "Spectral continuation taper octaves");
    ValidateResolutionFade(options.fade);
}

/// The impulses of one band over a window of its lattice cells.
struct SpectralContinuationLayer::ImpulseTable {
    struct Impulse {
        double x = 0.0;  ///< Center, in meters.
        double y = 0.0;
        double kx = 0.0;  ///< Angular wave vector, in radians per meter.
        double ky = 0.0;
        double phase = 0.0;
        double amplitude = 0.0;  ///< Meters, faded for the sample spacing.
    };

    const Band* band = nullptr;
    std::int64_t col0 = 0;
    std::int64_t row0 = 0;
    std::int64_t cols = 0;
    std::vector<Impulse> impulses;  ///< `kImpulsesPerCell` per cell, row by row.
};

SpectralContinuationLayer::SpectralContinuationLayer(SpectralContinuationOptions options,
                                                     Surface surface)
    : options_(options), surface_(surface) {
    ValidateSpectralContinuationOptions(options_);
    const double nyquist = 0.5 / options_.pixelSize;
    coefficient_ = Coefficient(options_.slope, options_.octaveRms, nyquist);
    const double octaves = std::log2(1.0 / (options_.minWavelength * nyquist));
    const int count = static_cast<int>(
        std::clamp(std::ceil(octaves - 1e-9), 0.0, static_cast<double>(kMaxBands)));
    const double radius = std::sqrt(-std::log(kEnvelopeCut) / kPi);
    for (int i = 0; i < count; ++i) {
        Band band;
        band.frequency = std::ldexp(nyquist, i);
        band.bandwidth = kBandwidth * band.frequency;
        band.reach = radius / band.bandwidth;
        band.index = static_cast<std::uint32_t>(i);
        bands_.push_back(band);
    }
}

double SpectralContinuationLayer::TargetPsd(double frequency) const {
    const double nyquist = 0.5 / options_.pixelSize;
    if (!(frequency > nyquist) || frequency > 1.0 / options_.minWavelength) {
        return 0.0;
    }
    const double taper = Smoothstep(std::log2(frequency / nyquist) / options_.taperOctaves);
    return coefficient_ * std::pow(frequency, -options_.slope) * taper;
}

std::vector<double> SpectralContinuationLayer::BandFrequencies() const {
    std::vector<double> frequencies;
    for (const Band& band : bands_) {
        frequencies.push_back(band.frequency);
    }
    return frequencies;
}

SpectralContinuationLayer::ImpulseTable SpectralContinuationLayer::Table(const Band& band,
                                                                         double minX, double minY,
                                                                         double maxX, double maxY,
                                                                         double spacing) const {
    ImpulseTable table;
    table.band = &band;
    table.col0 = static_cast<std::int64_t>(std::floor(minX / band.reach)) - 1;
    table.row0 = static_cast<std::int64_t>(std::floor(minY / band.reach)) - 1;
    table.cols = static_cast<std::int64_t>(std::floor(maxX / band.reach)) + 2 - table.col0;
    const std::int64_t rows =
        static_cast<std::int64_t>(std::floor(maxY / band.reach)) + 2 - table.row0;
    const double density = kImpulsesPerCell / (band.reach * band.reach);
    const double weight =
        4.0 * kPi * std::log(2.0) * band.bandwidth * band.bandwidth / (density * EnvelopeEnergy());
    table.impulses.reserve(static_cast<std::size_t>(table.cols * rows * kImpulsesPerCell));
    for (std::int64_t row = table.row0; row < table.row0 + rows; ++row) {
        for (std::int64_t col = table.col0; col < table.col0 + table.cols; ++col) {
            const auto high = static_cast<std::uint32_t>((col >> 32) ^ (row >> 32));
            RandomStream random(options_.seed, kSpectralContinuationStream,
                                static_cast<std::uint32_t>(col), static_cast<std::uint32_t>(row),
                                band.index | (high << 6));
            for (int k = 0; k < kImpulsesPerCell; ++k) {
                ImpulseTable::Impulse impulse;
                impulse.x = (static_cast<double>(col) + random.NextDouble()) * band.reach;
                impulse.y = (static_cast<double>(row) + random.NextDouble()) * band.reach;
                const double frequency = band.frequency * std::exp2(random.NextDouble());
                const double angle = 2.0 * kPi * random.NextDouble();
                impulse.phase = 2.0 * kPi * random.NextDouble();
                impulse.kx = 2.0 * kPi * frequency * std::cos(angle);
                impulse.ky = 2.0 * kPi * frequency * std::sin(angle);
                impulse.amplitude =
                    std::sqrt(weight * frequency * frequency * TargetPsd(frequency)) *
                    ResolutionWeight(1.0 / frequency, spacing, options_.fade);
                table.impulses.push_back(impulse);
            }
        }
    }
    return table;
}

void SpectralContinuationLayer::AddDetail(const std::vector<Vec2d>& positions,
                                          const std::vector<float*>& heights,
                                          double spacing) const {
    if (positions.empty()) {
        return;
    }
    Vec2d low = positions[0];
    Vec2d high = positions[0];
    for (const Vec2d& p : positions) {
        low = {std::min(low.x, p.x), std::min(low.y, p.y)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y)};
    }
    std::vector<ImpulseTable> tables;
    for (const Band& band : bands_) {
        if (ResolutionWeight(1.0 / band.frequency, spacing, options_.fade) > 0.0) {
            tables.push_back(Table(band, low.x, low.y, high.x, high.y, spacing));
        }
    }
    for (std::size_t i = 0; i < positions.size(); ++i) {
        *heights[i] =
            static_cast<float>(static_cast<double>(*heights[i]) + DetailAt(tables, positions[i]));
    }
}

double SpectralContinuationLayer::DetailAt(const std::vector<ImpulseTable>& tables,
                                           const Vec2d& p) {
    const double cutScale = 1.0 / (1.0 - kEnvelopeCut);
    double detail = 0.0;
    for (const ImpulseTable& table : tables) {
        const Band& band = *table.band;
        const double reach2 = band.reach * band.reach;
        const double gauss = kPi * band.bandwidth * band.bandwidth;
        const auto col = static_cast<std::int64_t>(std::floor(p.x / band.reach));
        const auto row = static_cast<std::int64_t>(std::floor(p.y / band.reach));
        for (std::int64_t r = row - 1; r <= row + 1; ++r) {
            for (std::int64_t c = col - 1; c <= col + 1; ++c) {
                const auto first = static_cast<std::size_t>(
                    ((r - table.row0) * table.cols + (c - table.col0)) * kImpulsesPerCell);
                for (std::size_t k = first; k < first + kImpulsesPerCell; ++k) {
                    const ImpulseTable::Impulse& impulse = table.impulses[k];
                    const double dx = p.x - impulse.x;
                    const double dy = p.y - impulse.y;
                    const double d2 = dx * dx + dy * dy;
                    if (d2 >= reach2 || impulse.amplitude == 0.0) {
                        continue;
                    }
                    const double envelope = (std::exp(-gauss * d2) - kEnvelopeCut) * cutScale;
                    detail += impulse.amplitude * envelope *
                              std::cos(impulse.kx * dx + impulse.ky * dy + impulse.phase);
                }
            }
        }
    }
    return detail;
}

void SpectralContinuationLayer::ApplySamples(SampleBlock& samples,
                                             const LayerContext& context) const {
    const double spacing = samples.ResolvedSpacing(context.spacing, surface_);
    if (bands_.empty() ||
        !(ResolutionWeight(1.0 / bands_.front().frequency, spacing, options_.fade) > 0.0)) {
        return;
    }
    const double unit = surface_.UnitLength();
    Raster<float>* grid = samples.Grid();
    if (!grid) {
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            if (!samples.IsNoData(i)) {
                AddDetail({samples.Position(i) * unit}, {&samples.Height(i)}, spacing);
            }
        }
        return;
    }
    const std::size_t tilesX = (grid->Width() + kTileCells - 1) / kTileCells;
    const std::size_t tilesY = (grid->Height() + kTileCells - 1) / kTileCells;
    ParallelFor(tilesX * tilesY, context.threads, [&](std::size_t tile) {
        const std::size_t colBegin = (tile % tilesX) * kTileCells;
        const std::size_t rowBegin = (tile / tilesX) * kTileCells;
        const std::size_t colEnd = std::min(colBegin + kTileCells, grid->Width());
        const std::size_t rowEnd = std::min(rowBegin + kTileCells, grid->Height());
        std::vector<Vec2d> positions;
        std::vector<float*> heights;
        for (std::size_t row = rowBegin; row < rowEnd; ++row) {
            for (std::size_t col = colBegin; col < colEnd; ++col) {
                float& height = (*grid)(col, row);
                if (!grid->IsMissing(height)) {
                    positions.push_back(grid->CellCenter(col, row) * unit);
                    heights.push_back(&height);
                }
            }
        }
        AddDetail(positions, heights, spacing);
    });
}

}  // namespace terraingen
