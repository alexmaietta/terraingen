/// @file
/// @brief `Resample`, a raster resampled whole to square cells, the reference that sources'
/// upsampling is checked against in tests.
#pragma once

#include "terraingen/raster/grid.h"
#include "terraingen/raster/resample.h"
#include "terraingen/util/arithmetic.h"

#include <cmath>
#include <stdexcept>

namespace terraingen::testing {

/// A raster resampled to square cells of `pixelSize` world units over the same extent, origin and
/// axis directions (`ResampledGrid`), keeping its no-data value: each cell center interpolated
/// with `method`, cells beyond the edge read from the nearest edge cell, and cells with a no-data
/// contributing cell no-data (NaN without a no-data value).
/// @throws std::invalid_argument for an empty or rotated raster, or as `ResampledGrid`.
inline Raster<float> Resample(const Raster<float>& raster, double pixelSize,
                              ResampleMethod method) {
    if (raster.Empty() || raster.Transform().IsRotated()) {
        throw std::invalid_argument("Cannot resample an empty or rotated raster");
    }
    Raster<float> result = ResampledGrid(RasterGrid::FromRaster(raster), pixelSize).MakeRaster();
    result.SetNoData(raster.NoData());
    const double scaleX = std::fabs(raster.Transform().pixelWidth) / pixelSize;
    const double scaleY = std::fabs(raster.Transform().pixelHeight) / pixelSize;
    const float fill = raster.NoData().value_or(std::nanf(""));
    const auto value = [&](double col, double row) {
        return raster(ClampIndex(col, raster.Width()), ClampIndex(row, raster.Height()));
    };
    const auto missing = [&](float cell) { return raster.IsNoData(cell); };
    for (std::size_t row = 0; row < result.Height(); ++row) {
        const double v = (static_cast<double>(row) + 0.5) / scaleY - 0.5;
        for (std::size_t col = 0; col < result.Width(); ++col) {
            const double u = (static_cast<double>(col) + 0.5) / scaleX - 0.5;
            result(col, row) = InterpolateLattice(u, v, method, value, missing).value_or(fill);
        }
    }
    return result;
}

}  // namespace terraingen::testing
