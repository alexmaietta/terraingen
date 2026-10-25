// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/roughness_estimate.h"

#include "terraingen/raster/grid.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

/// Largest side, in cells, of the lattice a source is sampled on.
constexpr double kLatticeCells = 512.0;

/// Points a side of the lattice a noise layer's RMS height difference is measured on.
constexpr int kNoisePoints = 64;

/// Spacing of that lattice in wavelengths: the golden ratio conjugate, so the points fall at
/// every phase of the layer's octave lattices.
constexpr double kNoiseStep = 0.6180339887498949;

/// The roughness of `heights` on cells `cellSize` meters across by the variogram method.
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

}  // namespace

double BaseRoughness::Deviation(double lag) const {
    return deviation * std::pow(lag / cellSize, hurst);
}

double BaseRoughness::RmsSlope(double baseline) const {
    return Deviation(baseline) / baseline;
}

Raster<float> SampleSourceWindow(const Source& source, double cellSize, const Vec2d& origin) {
    const std::optional<RasterGrid> bounded = BoundedGrid(source);
    Bounds2d bounds;
    if (bounded) {
        bounds = bounded->Bounds();
    } else {
        const Vec2d center = source.ReferencePoint();
        const double half = 0.5 * kLatticeCells * cellSize;
        bounds = {center.x - half, center.y - half, center.x + half, center.y + half};
    }
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
    source.Fill(heights, cellSize);
    return heights;
}

BaseRoughness EstimateSourceRoughness(const Source& source, const Surface& surface) {
    const RasterGrid grid = source.Grid();
    if (!BoundedGrid(source)) {
        const Vec2d center = source.ReferencePoint();
        const double native = source.ResolutionAt(center.x, center.y);
        if (!std::isfinite(native)) {
            throw std::invalid_argument(
                "Estimating roughness needs a source with posts, such as a DEM or synthetic noise "
                "with a pixel size; give the values instead");
        }
        const Raster<float> heights =
            SampleSourceWindow(source, native, grid.transform.PixelToWorld(0.5, 0.5));
        return EstimateBaseRoughness(heights, native * surface.UnitLength());
    }
    const Bounds2d bounds = grid.Bounds();
    const double native =
        source.ResolutionAt(0.5 * (bounds.minX + bounds.maxX), 0.5 * (bounds.minY + bounds.maxY));
    const double cellSize = std::isfinite(native) ? native : grid.PixelSize();
    const Vec2d corner = grid.transform.PixelToWorld(0.0, 0.0);
    const Vec2d origin{corner.x + 0.5 * cellSize, corner.y - 0.5 * cellSize};
    const Raster<float> heights = SampleSourceWindow(source, cellSize, origin);
    return EstimateBaseRoughness(heights, cellSize * surface.UnitLength());
}

float MatchedNoiseAmplitude(const Fractal& fractal, const BaseRoughness& roughness) {
    Fractal unit = fractal;
    unit.amplitude = 1.0f;
    const double wavelength = static_cast<double>(fractal.wavelength);
    double sum = 0.0;
    for (int row = 0; row < kNoisePoints; ++row) {
        for (int col = 0; col < kNoisePoints; ++col) {
            const double x = kNoiseStep * wavelength * col;
            const double y = kNoiseStep * wavelength * row;
            const auto at = [&](double px, double py) {
                return static_cast<double>(
                    EvaluateFractal(static_cast<float>(px), static_cast<float>(py), unit));
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
