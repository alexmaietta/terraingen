#include "terraingen/layers/roughness_estimate.h"

#include "terraingen/raster/grid.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

/// The longest lag, in cells, the deviogram is fitted over.
constexpr std::size_t kMaxVariogramLag = 8;

/// Largest side, in cells, of the lattice a source is sampled on.
constexpr double kLatticeCells = 512.0;

/// Points a side of the lattice a noise layer's RMS height difference is measured on.
constexpr int kNoisePoints = 64;

/// Spacing of that lattice in wavelengths: the golden ratio conjugate, so the points fall at
/// every phase of the layer's octave lattices.
constexpr double kNoiseStep = 0.6180339887498949;

}  // namespace

bool HasData(const Raster<float>& heights, std::size_t col, std::size_t row) {
    const float value = heights(col, row);
    return std::isfinite(value) && !heights.IsNoData(value);
}

std::pair<double, std::size_t> Variogram(const Raster<float>& heights, std::size_t lag) {
    double sum = 0.0;
    std::size_t count = 0;
    const auto add = [&](std::size_t col, std::size_t row, std::size_t col2, std::size_t row2) {
        if (HasData(heights, col, row) && HasData(heights, col2, row2)) {
            const double difference =
                static_cast<double>(heights(col2, row2)) - static_cast<double>(heights(col, row));
            sum += difference * difference;
            ++count;
        }
    };
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            if (col + lag < heights.Width()) {
                add(col, row, col + lag, row);
            }
            if (row + lag < heights.Height()) {
                add(col, row, col, row + lag);
            }
        }
    }
    return {count > 0 ? sum / static_cast<double>(count) : 0.0, count};
}

DeviogramFit FitDeviogram(const Raster<float>& heights) {
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXX = 0.0;
    double sumXY = 0.0;
    int lags = 0;
    for (std::size_t lag = 1; lag <= kMaxVariogramLag; lag *= 2) {
        const auto [variogram, pairs] = Variogram(heights, lag);
        if (pairs == 0 || !(variogram > 0.0)) {
            break;
        }
        const double x = std::log2(static_cast<double>(lag));
        const double y = 0.5 * std::log2(variogram);
        sumX += x;
        sumY += y;
        sumXX += x * x;
        sumXY += x * y;
        ++lags;
    }
    DeviogramFit fit;
    fit.lags = lags;
    if (lags >= 2) {
        fit.slope = (lags * sumXY - sumX * sumY) / (lags * sumXX - sumX * sumX);
        fit.intercept = (sumY - fit.slope * sumX) / lags;
    }
    return fit;
}

double BaseRoughness::Deviation(double lag) const {
    return deviation * std::pow(lag / cellSize, hurst);
}

double BaseRoughness::RmsSlope(double baseline) const {
    return Deviation(baseline) / baseline;
}

BaseRoughness EstimateBaseRoughness(const Raster<float>& heights, double cellSize) {
    RequirePositive(cellSize, "Base roughness cell size");
    const DeviogramFit fit = FitDeviogram(heights);
    if (fit.lags < 2) {
        throw std::invalid_argument(
            "Estimating roughness from a base needs relief at lags of 1 and 2 cells; the base "
            "is flat or has too little data, so give the values instead");
    }
    if (!(fit.slope > 0.0)) {
        throw std::invalid_argument("The base's variogram gives a Hurst exponent of " +
                                    std::to_string(fit.slope) +
                                    ", not positive, so give the Hurst exponent instead");
    }
    BaseRoughness roughness;
    roughness.hurst = std::min(fit.slope, 1.0);
    roughness.deviation = std::exp2(fit.intercept);
    roughness.cellSize = cellSize;
    return roughness;
}

Raster<float> SampleSourceWindow(const Source& source, double cellSize, const Vec2d& origin) {
    const Bounds2d bounds = source.Grid().Bounds();
    const auto cells = [&](double extent) {
        return std::clamp(std::floor(extent / cellSize), 3.0, kLatticeCells);
    };
    const double width = cells(bounds.maxX - bounds.minX);
    const double height = cells(bounds.maxY - bounds.minY);
    const double col0 =
        std::round((0.5 * (bounds.minX + bounds.maxX) - origin.x) / cellSize - 0.5 * width);
    const double rowTop =
        std::round((0.5 * (bounds.minY + bounds.maxY) - origin.y) / cellSize + 0.5 * height);
    const RasterGrid grid{static_cast<std::size_t>(width), static_cast<std::size_t>(height),
                          GeoTransform::NorthUp(origin.x + (col0 - 0.5) * cellSize,
                                                origin.y + (rowTop + 0.5) * cellSize, cellSize)};
    Raster<float> heights = grid.MakeRaster();
    source.Fill(heights);
    return heights;
}

BaseRoughness EstimateSourceRoughness(const Source& source, const Surface& surface) {
    const RasterGrid grid = source.Grid();
    const Bounds2d bounds = grid.Bounds();
    const double native =
        source.ResolutionAt(0.5 * (bounds.minX + bounds.maxX), 0.5 * (bounds.minY + bounds.maxY));
    const double cellSize = std::isfinite(native) ? native : grid.PixelSize();
    const Vec2d corner = grid.transform.PixelToWorld(0.0, 0.0);
    const Vec2d origin{corner.x + 0.5 * cellSize, corner.y - 0.5 * cellSize};
    const Raster<float> heights = SampleSourceWindow(source, cellSize, origin);
    return EstimateBaseRoughness(heights, cellSize * surface.UnitLength());
}

float MatchedNoiseAmplitude(const NoiseLayer& layer, const BaseRoughness& roughness) {
    NoiseLayer unit = layer;
    unit.fractal.amplitude = 1.0f;
    const double wavelength = static_cast<double>(layer.fractal.wavelength);
    double sum = 0.0;
    for (int row = 0; row < kNoisePoints; ++row) {
        for (int col = 0; col < kNoisePoints; ++col) {
            const double x = kNoiseStep * wavelength * col;
            const double y = kNoiseStep * wavelength * row;
            const auto at = [&](double px, double py) {
                return static_cast<double>(
                    EvaluateNoiseLayer(static_cast<float>(px), static_cast<float>(py), unit));
            };
            const double here = at(x, y);
            const double east = at(x + wavelength, y) - here;
            const double north = at(x, y + wavelength) - here;
            sum += east * east + north * north;
        }
    }
    const double unitDeviation = std::sqrt(sum / (2.0 * kNoisePoints * kNoisePoints));
    if (!(unitDeviation > 0.0)) {
        throw std::invalid_argument(
            "The noise layer shows no relief to match the base's roughness to; give its "
            "amplitude instead");
    }
    return static_cast<float>(roughness.Deviation(wavelength) / unitDeviation);
}

}  // namespace terraingen
