#include "terraingen/layers/roughness_estimate.h"

#include "terraingen/random/stream.h"
#include "terraingen/sources/raster_source.h"
#include "terraingen/sources/synthetic_source.h"
#include "terraingen/util/bits.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

using terraingen::BaseRoughness;
using terraingen::GeoTransform;
using terraingen::Raster;

namespace {

/// One cosine of a self-affine sum.
struct Wave {
    double amplitude;
    double kx;
    double ky;
    double phase;
};

/// A self-affine surface of Hurst exponent `hurst`: cosines in 8 random directions at each of
/// wavelengths halving by sqrt(2) from 256 m to 1/32 m, amplitudes growing as wavelength^H.
class SelfAffineSurface {
  public:
    explicit SelfAffineSurface(double hurst) {
        for (std::uint32_t n = 0; n < 27; ++n) {
            const double wavelength = 256.0 * std::pow(2.0, -0.5 * n);
            for (std::uint32_t j = 0; j < 8; ++j) {
                terraingen::RandomStream random(7, 0, n, j);
                const double turn = 2.0 * terraingen::kPi * random.NextDouble();
                const double k = 2.0 * terraingen::kPi / wavelength;
                waves_.push_back({0.1 * std::pow(wavelength, hurst), k * std::cos(turn),
                                  k * std::sin(turn), 2.0 * terraingen::kPi * random.NextDouble()});
            }
        }
    }

    double At(double x, double y) const {
        double sum = 0.0;
        for (const Wave& wave : waves_) {
            sum += wave.amplitude * std::cos(wave.kx * x + wave.ky * y + wave.phase);
        }
        return sum;
    }

    /// The RMS height difference at a lag of `lag` meters, averaged along x and y.
    double Deviation(double lag) const {
        double sum = 0.0;
        for (const Wave& wave : waves_) {
            sum += wave.amplitude * wave.amplitude *
                   (2.0 - std::cos(wave.kx * lag) - std::cos(wave.ky * lag)) / 2.0;
        }
        return std::sqrt(sum);
    }

    /// The surface over `size` x `size` cells of `cellSize` meters.
    Raster<float> Sample(std::size_t size, double cellSize) const {
        Raster<float> heights(size, size);
        heights.SetTransform(
            GeoTransform::NorthUp(0.0, cellSize * static_cast<double>(size), cellSize));
        for (std::size_t row = 0; row < size; ++row) {
            for (std::size_t col = 0; col < size; ++col) {
                const auto center = heights.CellCenter(col, row);
                heights(col, row) = static_cast<float>(At(center.x, center.y));
            }
        }
        return heights;
    }

  private:
    std::vector<Wave> waves_;
};

/// The RMS height difference in meters of `layer` at a lag of its wavelength on a lattice unlike
/// the one `MatchedNoiseAmplitude` measures on.
double LayerDeviation(const terraingen::NoiseLayer& layer) {
    const double lag = static_cast<double>(layer.fractal.wavelength);
    double sum = 0.0;
    int count = 0;
    for (int row = 0; row < 80; ++row) {
        for (int col = 0; col < 80; ++col) {
            const double x = 3.1 + 0.37 * lag * col;
            const double y = -7.9 + 0.41 * lag * row;
            const auto at = [&](double px, double py) {
                return static_cast<double>(layer.fractal.amplitude) *
                       static_cast<double>(terraingen::EvaluateNoiseLayer(
                           static_cast<float>(px), static_cast<float>(py), layer));
            };
            const double difference = at(x + lag, y) - at(x, y);
            sum += difference * difference;
            ++count;
        }
    }
    return std::sqrt(sum / count);
}

/// `EstimateSourceRoughness` of `heights` read as a raster source of `cellSize` cells.
BaseRoughness Estimate(Raster<float> heights, double cellSize) {
    heights.SetTransform(
        GeoTransform::NorthUp(0.0, cellSize * static_cast<double>(heights.Height()), cellSize));
    return terraingen::EstimateSourceRoughness(terraingen::RasterSource(heights));
}

}  // namespace

TEST(EstimateSourceRoughness, ReadsARampAsSmoothWithItsSlope) {
    Raster<float> ramp(32, 32);
    for (std::size_t row = 0; row < ramp.Height(); ++row) {
        for (std::size_t col = 0; col < ramp.Width(); ++col) {
            ramp(col, row) = 0.5f * static_cast<float>(col);
        }
    }
    const BaseRoughness roughness = Estimate(ramp, 2.0);
    EXPECT_NEAR(roughness.hurst, 1.0, 1e-9);
    EXPECT_NEAR(roughness.deviation, 0.5 / std::sqrt(2.0), 1e-9);
    EXPECT_EQ(roughness.cellSize, 2.0);
    EXPECT_NEAR(roughness.Deviation(8.0), 2.0 / std::sqrt(2.0), 1e-9);
    EXPECT_NEAR(roughness.RmsSlope(0.1), 0.25 / std::sqrt(2.0), 1e-9);
}

TEST(EstimateSourceRoughness, RecoversTheHurstExponentAndRmsSlopeOfSelfAffineSurfaces) {
    for (const double hurst : {0.5, 0.7, 0.9}) {
        const SelfAffineSurface surface(hurst);
        const BaseRoughness roughness = Estimate(surface.Sample(128, 1.0), 1.0);
        EXPECT_NEAR(roughness.hurst, hurst, 0.08) << hurst;
        EXPECT_NEAR(roughness.deviation, surface.Deviation(1.0), 0.15 * surface.Deviation(1.0));
        for (const double baseline : {0.5, 0.25, 0.1}) {
            const double expected = surface.Deviation(baseline) / baseline;
            EXPECT_NEAR(roughness.RmsSlope(baseline), expected, 0.25 * expected)
                << hurst << " " << baseline;
        }
    }
}

TEST(EstimateSourceRoughness, RejectsFlatBases) {
    EXPECT_THROW(Estimate(Raster<float>(16, 16, 3.0f), 1.0), std::invalid_argument);
    Raster<float> checkerboard(16, 16);
    for (std::size_t row = 0; row < 16; ++row) {
        for (std::size_t col = 0; col < 16; ++col) {
            checkerboard(col, row) = static_cast<float>((col + row) % 2);
        }
    }
    EXPECT_THROW(Estimate(checkerboard, 1.0), std::invalid_argument);
}

TEST(EstimateSourceRoughness, SkipsCellsWithoutData) {
    Raster<float> heights = SelfAffineSurface(0.7).Sample(64, 1.0);
    const BaseRoughness whole = Estimate(heights, 1.0);
    heights.SetNoData(-9999.0f);
    heights(10, 10) = -9999.0f;
    heights(20, 30) = std::nanf("");
    const BaseRoughness holed = Estimate(heights, 1.0);
    EXPECT_NEAR(holed.hurst, whole.hurst, 0.01);
    EXPECT_NEAR(holed.deviation, whole.deviation, 0.01 * whole.deviation);
}

TEST(EstimateSourceRoughness, ReadsTheSourceOnItsNativeCells) {
    const Raster<float> heights = SelfAffineSurface(0.6).Sample(96, 2.0);
    const terraingen::RasterSource source(heights);
    const BaseRoughness direct = Estimate(heights, 2.0);
    const BaseRoughness read = terraingen::EstimateSourceRoughness(source);
    EXPECT_NEAR(read.hurst, direct.hurst, 0.01) << "the lattice is centered on the grid";
    EXPECT_NEAR(read.deviation, direct.deviation, 0.01 * direct.deviation);
    EXPECT_EQ(read.cellSize, 2.0);

    terraingen::RasterSourceOptions smoothed;
    smoothed.effectivePixelSize = 4.0;
    const BaseRoughness coarser =
        terraingen::EstimateSourceRoughness(terraingen::RasterSource(heights, smoothed));
    EXPECT_EQ(coarser.cellSize, 4.0) << "read on cells of the effective pixel size";
    EXPECT_NEAR(coarser.hurst, direct.hurst, 0.15);

    terraingen::NoiseLayer layer;
    layer.fractal.wavelength = 200.0f;
    layer.fractal.amplitude = 20.0f;
    layer.fractal.octaves = 6;
    const terraingen::SyntheticSource posts(terraingen::SyntheticTerrainParams{{layer}}, 0.0f, 2.0);
    EXPECT_EQ(terraingen::EstimateSourceRoughness(posts).cellSize, 2.0)
        << "synthetic noise is read on its posts";
    EXPECT_THROW(terraingen::EstimateSourceRoughness(
                     terraingen::SyntheticSource(terraingen::SyntheticTerrainParams{{layer}})),
                 std::invalid_argument)
        << "noise without posts has no cell size to read it on";

    const terraingen::Surface sphere = terraingen::Surface::Sphere(1000.0);
    EXPECT_DOUBLE_EQ(terraingen::EstimateSourceRoughness(source, sphere).cellSize,
                     2.0 * sphere.UnitLength());
}

TEST(MatchedNoiseAmplitude, GivesTheLayerTheBaseDeviationAtItsWavelength) {
    terraingen::NoiseLayer layer;
    layer.fractal.wavelength = 10.0f;
    layer.fractal.octaves = 4;
    layer.fractal.seed = 5;
    layer.fractal.gain = terraingen::GainFromHurst(0.7f, 2.0f);
    BaseRoughness roughness;
    roughness.hurst = 0.7;
    roughness.deviation = 0.3;
    roughness.cellSize = 2.0;
    const float amplitude = terraingen::MatchedNoiseAmplitude(layer, roughness);
    terraingen::NoiseLayer matched = layer;
    matched.fractal.amplitude = amplitude;
    EXPECT_NEAR(LayerDeviation(matched), roughness.Deviation(10.0),
                0.1 * roughness.Deviation(10.0));
    EXPECT_EQ(terraingen::MatchedNoiseAmplitude(layer, roughness), amplitude);
    roughness.deviation = 0.6;
    EXPECT_FLOAT_EQ(terraingen::MatchedNoiseAmplitude(layer, roughness), 2.0f * amplitude);
}
