// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `ResampledGrid` and `Resample`, a raster resampled whole to square cells, the
/// reference that sources' upsampling is checked against in tests.
#pragma once

#include "terraingen/raster/grid.h"
#include "terraingen/raster/resample.h"
#include "terraingen/util/bits.h"

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace terraingen::testing {

/// A grid with square cells over the same extent, origin and axis directions as an unrotated
/// `grid`: round(width * |pixelWidth| / `pixelSize`) x round(height * |pixelHeight| /
/// `pixelSize`) cells, at least one each.
/// @param pixelSize The new cell side in world units; positive and finite.
/// @throws std::invalid_argument for a rotated grid, a `pixelSize` that is not positive and finite,
/// or a result wider or taller than 2^31 cells.
inline RasterGrid ResampledGrid(const RasterGrid& grid, double pixelSize) {
    if (!(pixelSize > 0.0) || !std::isfinite(pixelSize)) {
        throw std::invalid_argument("Pixel size must be positive and finite");
    }
    const GeoTransform& source = grid.transform;
    if (source.IsRotated()) {
        throw std::invalid_argument("Cannot resample a rotated raster");
    }
    const double scaleX = std::fabs(source.pixelWidth) / pixelSize;
    const double scaleY = std::fabs(source.pixelHeight) / pixelSize;
    const double width = std::fmax(1.0, std::round(static_cast<double>(grid.width) * scaleX));
    const double height = std::fmax(1.0, std::round(static_cast<double>(grid.height) * scaleY));
    if (width > 2147483647.0 || height > 2147483647.0) {
        throw std::invalid_argument("Resampled raster would be too large");
    }
    RasterGrid resampled{static_cast<std::size_t>(width), static_cast<std::size_t>(height), source};
    resampled.transform.pixelWidth = std::copysign(pixelSize, source.pixelWidth);
    resampled.transform.pixelHeight = std::copysign(pixelSize, source.pixelHeight);
    return resampled;
}

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
