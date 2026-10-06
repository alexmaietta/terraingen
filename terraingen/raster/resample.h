#pragma once

#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"

namespace terraingen {

/// How `Resample` interpolates between source cells.
enum class ResampleMethod {
    Nearest,   ///< The value of the nearest cell centre.
    Bilinear,  ///< Linear in each axis over the four surrounding cells.
    Cubic,     ///< Keys cubic convolution (a = -0.5) over the sixteen surrounding cells.
};

/// `raster` resampled to square cells of `pixelSize` world units over the same extent: the result
/// has round(width * |pixelWidth| / `pixelSize`) x round(height * |pixelHeight| / `pixelSize`)
/// cells (at least one each), the same origin and the same axis directions.
///
/// Each output cell centre is mapped into the source and interpolated, with samples beyond the
/// edge taken from the nearest edge cell. Where any contributing cell holds the no-data value, the
/// output cell is no-data. The CRS and no-data value are kept.
///
/// Reference: R. Keys, "Cubic convolution interpolation for digital image processing", IEEE
/// Transactions on Acoustics, Speech, and Signal Processing 29(6):1153-1160, 1981.
/// https://doi.org/10.1109/TASSP.1981.1163711
/// @throws std::invalid_argument for an empty or rotated raster, a non-positive or non-finite
/// `pixelSize`, or a result wider or taller than 2^31 cells.
Raster<float> Resample(const Raster<float>& raster, double pixelSize, ResampleMethod method);

/// `raster` interpolated at world position (`x`, `y`) with `method`, as `Resample` interpolates:
/// samples beyond the edge come from the nearest edge cell, and any contributing no-data cell gives
/// NaN. Positions outside the raster's extent give NaN.
/// @throws std::invalid_argument for an empty or rotated raster.
float SampleRaster(const Raster<float>& raster, double x, double y, ResampleMethod method);

/// `raster` resampled onto `grid`: each cell is `SampleRaster` at its centre, and cells that give
/// NaN hold `raster`'s no-data value (NaN if it has none, which then becomes the no-data value).
/// The CRS is kept.
/// @throws std::invalid_argument for an empty or rotated raster, or an empty grid.
Raster<float> ResampleTo(const Raster<float>& raster, const RasterGrid& grid,
                         ResampleMethod method);

}  // namespace terraingen
