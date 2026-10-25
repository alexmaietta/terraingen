// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/spectral_continuation.h"

#include "terraingen/layers/lattice.h"
#include "terraingen/random/stream.h"
#include "terraingen/random/streams.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

constexpr double kMinSlope = 2.0;
constexpr double kMaxSlope = 4.0;

/// The Gaussian envelope parameter a of a band's impulses over the band's lowest frequency: the
/// impulses' spectra are Gaussians of standard deviation a / (2 sqrt(pi)), about a seventh of it.
constexpr double kBandwidth = 0.5;
/// The envelope's value at the impulse radius, where it is cut and shifted to reach zero.
constexpr double kEnvelopeCut = 0.01;
constexpr int kImpulsesPerCell = 8;
constexpr int kMaxBands = 40;

/// Grid cells per side of the tiles a grid is evaluated in.
constexpr std::size_t kTileCells = 64;

/// The integral of the squared envelope over the plane, times a^2: the envelope is
/// (exp(-pi a^2 d^2) - c) / (1 - c) inside the radius where the Gaussian falls to c.
double EnvelopeEnergy() {
    const double c = kEnvelopeCut;
    const double u = -std::log(c);
    return ((1.0 - c * c) / 2.0 - 2.0 * c * (1.0 - c) + c * c * u) / ((1.0 - c) * (1.0 - c));
}

}  // namespace

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
                                                     TerrainBase base)
    : options_(options), surface_(base.surface) {
    ValidateSpectralContinuationOptions(options_);
    const double nyquist = 0.5 / options_.pixelSize;
    coefficient_ = PowerLawSpectrum{options_.slope, options_.octaveRms}.Coefficient(nyquist);
    const double octaves = std::log2(1.0 / (options_.minWavelength * nyquist));
    const int count = static_cast<int>(
        std::clamp(std::ceil(octaves - 1e-9), 0.0, static_cast<double>(kMaxBands)));
    const double radius = std::sqrt(-std::log(kEnvelopeCut) / kPi);
    for (int i = 0; i < count; ++i) {
        Band band;
        band.frequency = std::ldexp(nyquist, i);
        band.bandwidth = kBandwidth * band.frequency;
        band.reach = radius / band.bandwidth;
        band.seed = SubstreamSeed(options_.seed, kSpectralContinuationStream,
                                  static_cast<std::uint32_t>(i));
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
            RandomStream random = LatticeStream(band.seed, kSpectralContinuationStream, col, row);
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
