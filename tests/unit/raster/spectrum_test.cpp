// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/raster/spectrum.h"

#include "terraingen/random/samplers.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/fft.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::RasterGrid;
using terraingen::detail::SpectrumBin;

namespace {

/// A north-up grid of `size` x `size` cells of `step` meters with its top-left corner at
/// (-3, 7).
RasterGrid Grid(std::size_t size, double step) {
    return {size, size, GeoTransform::NorthUp(-3.0, 7.0, step)};
}

/// A periodic `size` x `size` surface of unit cells whose PSD is `coefficient` f^-`slope`, by
/// inverse FFT of random phases, taken `crop` cells a side from its corner.
Raster<float> PowerLawSurface(double slope, double coefficient, std::size_t size, std::size_t crop,
                              std::uint32_t seed) {
    std::vector<std::complex<double>> spectrum(size * size);
    const auto n = static_cast<double>(size);
    for (std::size_t v = 0; v < size; ++v) {
        for (std::size_t u = 0; u < size; ++u) {
            const double fu = static_cast<double>(u <= size / 2 ? u : size - u) / n;
            const double fv = static_cast<double>(v <= size / 2 ? v : size - v) / n;
            const double f = std::hypot(fu, fv);
            if (f == 0.0) {
                continue;
            }
            terraingen::RandomStream random(seed, 0, static_cast<std::uint32_t>(u),
                                            static_cast<std::uint32_t>(v));
            const double sigma = std::sqrt(coefficient * std::pow(f, -slope) / (n * n));
            spectrum[v * size + u] = {sigma * terraingen::StandardNormal(random),
                                      sigma * terraingen::StandardNormal(random)};
        }
    }
    terraingen::Fft2(spectrum, size, size, 1.0);
    Raster<float> heights(crop, crop);
    for (std::size_t row = 0; row < crop; ++row) {
        for (std::size_t col = 0; col < crop; ++col) {
            heights(col, row) = static_cast<float>(spectrum[row * size + col].real());
        }
    }
    return heights;
}

}  // namespace

TEST(RadialPowerSpectrum, PeaksAtAWaveAndIntegratesToTheVariance) {
    Raster<float> heights = Grid(64, 0.5).MakeRaster();
    for (std::size_t row = 0; row < 64; ++row) {
        for (std::size_t col = 0; col < 64; ++col) {
            heights(col, row) = static_cast<float>(
                2.0 * std::cos(2.0 * terraingen::kPi * static_cast<double>(col) / 8.0) +
                0.1 * static_cast<double>(col));
        }
    }
    const std::vector<SpectrumBin> bins = terraingen::detail::RadialPowerSpectrum(heights);
    ASSERT_FALSE(bins.empty());
    std::size_t peak = 0;
    for (std::size_t i = 0; i < bins.size(); ++i) {
        if (bins[i].power > bins[peak].power) {
            peak = i;
        }
    }
    EXPECT_NEAR(bins[peak].frequency, 0.25, 0.03) << "8 cells of 0.5 m";
    EXPECT_NEAR(bins.back().frequency, 1.0, 0.1) << "the Nyquist frequency";
}

TEST(RadialPowerSpectrum, RejectsSmallRotatedOrEmptyRasters) {
    EXPECT_THROW(terraingen::detail::RadialPowerSpectrum(Grid(7, 1.0).MakeRaster()),
                 std::invalid_argument);
    Raster<float> rotated = Grid(16, 1.0).MakeRaster();
    GeoTransform transform = rotated.Transform();
    transform.rowRotation = 0.1;
    rotated.SetTransform(transform);
    EXPECT_THROW(terraingen::detail::RadialPowerSpectrum(rotated), std::invalid_argument);
    Raster<float> empty = Grid(16, 1.0).MakeRaster(std::numeric_limits<float>::quiet_NaN());
    EXPECT_THROW(terraingen::detail::RadialPowerSpectrum(empty), std::invalid_argument);
}

TEST(EstimatePowerLawSpectrum, RecoversTheSlopeAndOctaveRmsOfPowerLawSurfaces) {
    for (const double slope : {2.4, 3.0, 3.6}) {
        constexpr double kCoefficient = 1e-3;
        const Raster<float> heights = PowerLawSurface(slope, kCoefficient, 1024, 256, 3);
        const terraingen::PowerLawSpectrum fit = terraingen::EstimatePowerLawSpectrum(heights);
        EXPECT_NEAR(fit.slope, slope, 0.12) << slope;
        EXPECT_FALSE(fit.clipped) << slope;
        const double integral =
            (std::pow(0.5, 2.0 - slope) * (std::pow(2.0, 2.0 - slope) - 1.0)) / (2.0 - slope);
        const double expected = std::sqrt(2.0 * terraingen::kPi * kCoefficient * integral);
        EXPECT_NEAR(fit.octaveRms / expected, 1.0, 0.15) << slope;
    }
}

TEST(EstimatePowerLawSpectrum, IgnoresATilt) {
    const Raster<float> level = PowerLawSurface(3.0, 1e-3, 1024, 256, 5);
    Raster<float> tilted = level;
    for (std::size_t row = 0; row < tilted.Height(); ++row) {
        for (std::size_t col = 0; col < tilted.Width(); ++col) {
            tilted(col, row) += static_cast<float>(0.5 * static_cast<double>(col) -
                                                   0.3 * static_cast<double>(row) + 40.0);
        }
    }
    const terraingen::PowerLawSpectrum flat = terraingen::EstimatePowerLawSpectrum(level);
    const terraingen::PowerLawSpectrum fit = terraingen::EstimatePowerLawSpectrum(tilted);
    EXPECT_NEAR(fit.slope, flat.slope, 0.01);
    EXPECT_NEAR(fit.octaveRms / flat.octaveRms, 1.0, 0.01);
    EXPECT_FALSE(fit.clipped);
}

TEST(EstimatePowerLawSpectrum, ReportsSlopesClampedToItsRange) {
    const terraingen::PowerLawSpectrum white =
        terraingen::EstimatePowerLawSpectrum(PowerLawSurface(0.0, 1.0, 256, 128, 6));
    EXPECT_EQ(white.slope, 2.0);
    EXPECT_TRUE(white.clipped);
    EXPECT_GT(white.octaveRms, 0.0);
    const terraingen::PowerLawSpectrum smooth =
        terraingen::EstimatePowerLawSpectrum(PowerLawSurface(5.0, 1e-3, 256, 128, 7));
    EXPECT_EQ(smooth.slope, 4.0);
    EXPECT_TRUE(smooth.clipped);
}

TEST(EstimatePowerLawSpectrum, HandlesFlatRastersHolesAndBadBands) {
    const terraingen::PowerLawSpectrum flat =
        terraingen::EstimatePowerLawSpectrum(Grid(64, 1.0).MakeRaster(4.0f));
    EXPECT_EQ(flat.slope, 3.0);
    EXPECT_EQ(flat.octaveRms, 0.0);
    EXPECT_FALSE(flat.clipped);

    Raster<float> holed = PowerLawSurface(3.0, 1e-3, 256, 128, 4);
    holed.SetNoData(-9999.0f);
    holed(10, 10) = -9999.0f;
    holed(60, 70) = std::numeric_limits<float>::quiet_NaN();
    EXPECT_NEAR(terraingen::EstimatePowerLawSpectrum(holed).slope, 3.0, 0.2);

    terraingen::SpectrumFitBand narrow;
    narrow.minPixels = 4.0;
    narrow.maxPixels = 4.5;
    EXPECT_THROW(terraingen::EstimatePowerLawSpectrum(holed, narrow), std::invalid_argument);
    terraingen::SpectrumFitBand bad;
    bad.minPixels = 1.0;
    EXPECT_THROW(terraingen::ValidateSpectrumFitBand(bad), std::invalid_argument);
    bad.minPixels = 8.0;
    bad.maxPixels = 6.0;
    EXPECT_THROW(terraingen::ValidateSpectrumFitBand(bad), std::invalid_argument);
    EXPECT_EQ(terraingen::SpectrumFitBand{}, terraingen::SpectrumFitBand{});
    EXPECT_NE(terraingen::SpectrumFitBand{}, bad);
}

TEST(PowerLawSpectrum, HoldsItsOctaveRmsInTheOctaveAboveNyquist) {
    const terraingen::PowerLawSpectrum spectrum{3.0, 0.2};
    const double coefficient = spectrum.Coefficient(0.5);
    double variance = 0.0;
    for (double f = 0.5; f < 1.0; f += 1e-5) {
        const double mid = f + 0.5e-5;
        variance += 2.0 * terraingen::kPi * mid * coefficient * std::pow(mid, -3.0) * 1e-5;
    }
    EXPECT_NEAR(std::sqrt(variance), 0.2, 1e-6);
}
