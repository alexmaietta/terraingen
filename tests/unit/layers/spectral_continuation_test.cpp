#include "terraingen/layers/spectral_continuation.h"

#include "terraingen/layers/sample_block.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/span.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::RasterGrid;
using terraingen::SpectralContinuationLayer;
using terraingen::SpectralContinuationOptions;
using terraingen::detail::SpectrumBin;

namespace {

SpectralContinuationOptions Options(double slope = 3.0) {
    SpectralContinuationOptions options;
    options.pixelSize = 1.0;
    options.slope = slope;
    options.octaveRms = 0.2;
    options.minWavelength = 0.25;
    options.seed = 5;
    return options;
}

/// A north-up grid of `size` x `size` cells of `step` meters with its top-left corner at
/// (`x0`, `y0`).
RasterGrid Grid(std::size_t size, double step, double x0 = -3.0, double y0 = 7.0) {
    return {size, size, GeoTransform::NorthUp(x0, y0, step)};
}

/// The detail the layer adds over `grid` at its own spacing.
Raster<float> Detail(const SpectralContinuationLayer& layer, const RasterGrid& grid,
                     double spacing = 0.0, unsigned threads = 1) {
    Raster<float> heights = grid.MakeRaster();
    terraingen::LayerContext context;
    context.spacing = spacing;
    context.threads = threads;
    layer.Apply(heights, context);
    return heights;
}

double Variance(const Raster<float>& heights) {
    double sum = 0.0;
    double squares = 0.0;
    for (const float value : heights.Values()) {
        sum += static_cast<double>(value);
        squares += static_cast<double>(value) * static_cast<double>(value);
    }
    const auto n = static_cast<double>(heights.Size());
    return squares / n - (sum / n) * (sum / n);
}

/// The least-squares slope of log power on log frequency over bins in [`low`, `high`].
double LogSlope(const std::vector<SpectrumBin>& bins, double low, double high) {
    double n = 0.0;
    double sx = 0.0;
    double sy = 0.0;
    double sxx = 0.0;
    double sxy = 0.0;
    for (const SpectrumBin& bin : bins) {
        if (bin.frequency < low || bin.frequency > high) {
            continue;
        }
        const double x = std::log(bin.frequency);
        const double y = std::log(bin.power);
        n += 1.0;
        sx += x;
        sy += y;
        sxx += x * x;
        sxy += x * y;
    }
    return (n * sxy - sx * sy) / (n * sxx - sx * sx);
}

/// The PSD in m^4 the layer of `options` targets at `frequency` cycles per meter: the power law,
/// tapered in above the base's Nyquist frequency, up to 1 / `minWavelength`.
double TargetPsd(const SpectralContinuationOptions& options, double frequency) {
    const double nyquist = 0.5 / options.pixelSize;
    if (!(frequency > nyquist) || frequency > 1.0 / options.minWavelength) {
        return 0.0;
    }
    const double coefficient =
        terraingen::PowerLawSpectrum{options.slope, options.octaveRms}.Coefficient(nyquist);
    return coefficient * std::pow(frequency, -options.slope) *
           terraingen::Smoothstep(std::log2(frequency / nyquist) / options.taperOctaves);
}

}  // namespace

TEST(SpectralContinuationOptions, RejectInvalidValues) {
    EXPECT_NO_THROW(terraingen::ValidateSpectralContinuationOptions(Options()));
    const auto rejects = [](auto change) {
        SpectralContinuationOptions options = Options();
        change(options);
        EXPECT_THROW(terraingen::ValidateSpectralContinuationOptions(options),
                     std::invalid_argument);
        EXPECT_THROW(SpectralContinuationLayer{options}, std::invalid_argument);
    };
    rejects([](SpectralContinuationOptions& o) { o.pixelSize = 0.0; });
    rejects([](SpectralContinuationOptions& o) { o.slope = 1.9; });
    rejects([](SpectralContinuationOptions& o) { o.slope = 4.1; });
    rejects([](SpectralContinuationOptions& o) { o.slope = std::nan(""); });
    rejects([](SpectralContinuationOptions& o) { o.octaveRms = -0.1; });
    rejects([](SpectralContinuationOptions& o) { o.minWavelength = 0.0; });
    rejects([](SpectralContinuationOptions& o) { o.taperOctaves = 0.0; });
    rejects([](SpectralContinuationOptions& o) { o.fade.endSamples = 1.0; });
    SpectralContinuationOptions other = Options();
    EXPECT_EQ(Options(), other);
    other.seed = 6;
    EXPECT_NE(Options(), other);
}

TEST(SpectralContinuationLayer, AddsNothingAtTheBaseSpacing) {
    const SpectralContinuationLayer layer(Options());
    const Raster<float> detail = Detail(layer, Grid(32, 1.0));
    for (const float value : detail.Values()) {
        EXPECT_EQ(value, 0.0f);
    }
    SpectralContinuationOptions coarse = Options();
    coarse.minWavelength = 2.5;
    const Raster<float> none = Detail(SpectralContinuationLayer(coarse), Grid(32, 0.1));
    EXPECT_EQ(Variance(none), 0.0);
}

TEST(SpectralContinuationLayer, MatchesTheTargetSpectrumAndAddsNoResolvedPower) {
    constexpr double kStep = 1.0 / 16.0;
    for (const double slope : {2.5, 3.0, 3.5}) {
        const SpectralContinuationLayer layer(Options(slope));
        std::vector<SpectrumBin> mean;
        constexpr int kSeeds = 2;
        for (int s = 0; s < kSeeds; ++s) {
            SpectralContinuationOptions options = Options(slope);
            options.seed = 11 + static_cast<std::uint32_t>(s);
            const Raster<float> detail =
                Detail(SpectralContinuationLayer(options), Grid(512, kStep), 0.0, 0);
            const std::vector<SpectrumBin> bins = terraingen::detail::RadialPowerSpectrum(detail);
            if (mean.empty()) {
                mean = bins;
                for (SpectrumBin& bin : mean) {
                    bin.power = 0.0;
                }
            }
            for (std::size_t i = 0; i < bins.size(); ++i) {
                mean[i].power += bins[i].power / kSeeds;
            }
        }
        EXPECT_NEAR(-LogSlope(mean, 1.1, 3.0), slope, 0.15) << slope;
        for (const SpectrumBin& bin : mean) {
            if (bin.frequency > 1.1 && bin.frequency < 3.0) {
                EXPECT_NEAR(bin.power / TargetPsd(layer.Options(), bin.frequency), 1.0, 0.25)
                    << slope << " at " << bin.frequency;
            }
            if (bin.frequency < 0.4) {
                const double resolved =
                    TargetPsd(layer.Options(), 1.0) * std::pow(bin.frequency, -slope);
                EXPECT_LT(bin.power, 1e-2 * resolved) << slope << " at " << bin.frequency;
            }
        }
    }
}

TEST(SpectralContinuationLayer, HoldsTheTargetVarianceAndFadesWithSpacing) {
    const SpectralContinuationLayer layer(Options());
    double target = 0.0;
    for (double f = 0.5; f < 4.0; f += 1e-4) {
        target +=
            2.0 * terraingen::kPi * (f + 0.5e-4) * TargetPsd(layer.Options(), f + 0.5e-4) * 1e-4;
    }
    const double fine = Variance(Detail(layer, Grid(512, 1.0 / 16.0), 0.0, 0));
    EXPECT_NEAR(fine / target, 1.0, 0.1);
    const double middle = Variance(Detail(layer, Grid(256, 1.0 / 16.0), 0.25, 0));
    const double coarse = Variance(Detail(layer, Grid(256, 1.0 / 16.0), 0.5, 0));
    EXPECT_LT(middle, fine);
    EXPECT_LT(coarse, middle);
    EXPECT_GT(coarse, 0.0);
    EXPECT_EQ(Variance(Detail(layer, Grid(64, 1.0 / 16.0), 1.0)), 0.0);
}

TEST(SpectralContinuationLayer, AgreesAcrossGridsTilesThreadsAndPoints) {
    const SpectralContinuationLayer layer(Options());
    constexpr double kStep = 0.125;
    const Raster<float> whole = Detail(layer, Grid(96, kStep), 0.0, 1);
    const Raster<float> threaded = Detail(layer, Grid(96, kStep), 0.0, 4);
    const Raster<float> tile = Detail(layer, Grid(40, kStep, -3.0 + 37 * kStep, 7.0 - 29 * kStep));
    std::vector<terraingen::Vec2d> points;
    for (std::size_t row = 0; row < 40; ++row) {
        for (std::size_t col = 0; col < 40; ++col) {
            ASSERT_EQ(tile(col, row), whole(col + 37, row + 29)) << col << ", " << row;
            points.push_back(tile.CellCenter(col, row));
        }
    }
    for (std::size_t i = 0; i < whole.Size(); ++i) {
        ASSERT_EQ(whole.Values()[i], threaded.Values()[i]) << i;
    }
    std::vector<float> heights(points.size(), 0.0f);
    terraingen::SampleBlock samples{terraingen::Span<const terraingen::Vec2d>(points),
                                    terraingen::Span<float>(heights)};
    terraingen::LayerContext context;
    context.spacing = kStep;
    layer.Apply(samples, context);
    for (std::size_t i = 0; i < points.size(); ++i) {
        ASSERT_EQ(heights[i], tile(i % 40, i / 40)) << i;
    }
    EXPECT_EQ(layer.ApplyAt(1.0f, points[5].x, points[5].y, context), 1.0f + heights[5]);
}

TEST(SpectralContinuationLayer, VariesWithTheSeedAndSkipsCellsWithoutData) {
    SpectralContinuationOptions other = Options();
    other.seed = 6;
    const Raster<float> a = Detail(SpectralContinuationLayer(Options()), Grid(32, 0.125));
    const Raster<float> b = Detail(SpectralContinuationLayer(other), Grid(32, 0.125));
    EXPECT_NE(a(10, 10), b(10, 10));

    Raster<float> heights = Grid(32, 0.125).MakeRaster(1.0f);
    heights.SetNoData(-9999.0f);
    heights(3, 4) = -9999.0f;
    heights(5, 6) = std::numeric_limits<float>::quiet_NaN();
    SpectralContinuationLayer(Options()).Apply(heights, terraingen::LayerContext{});
    EXPECT_EQ(heights(3, 4), -9999.0f);
    EXPECT_TRUE(std::isnan(heights(5, 6)));
    EXPECT_NE(heights(7, 7), 1.0f);
}

TEST(SpectralContinuationLayer, ScalesBodyCoordinatesToMeters) {
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    const double unit = moon.UnitLength();
    const SpectralContinuationLayer plane(Options());
    const SpectralContinuationLayer body(Options(), moon);
    terraingen::LayerContext context;
    context.spacing = 0.125;
    const double x = 37.0 / unit;
    const double y = -12.0 / unit;
    EXPECT_NEAR(body.ApplyAt(0.0f, x, y, context), plane.ApplyAt(0.0f, x * unit, y * unit, context),
                1e-5);
}
