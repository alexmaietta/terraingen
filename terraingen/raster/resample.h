#pragma once

#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>

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
/// output cell is no-data. The no-data value is kept.
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
/// @throws std::invalid_argument for an empty or rotated raster, or an empty grid.
Raster<float> ResampleTo(const Raster<float>& raster, const RasterGrid& grid,
                         ResampleMethod method);

/// @brief The no-data value of a raster filled from a source with no-data value `noData`, whose
/// missing cells hold `noData` or, without one, NaN.
/// @param noData The source's no-data value, if any.
/// @param missing Whether any cell was missing.
/// @return `noData` if set; otherwise NaN if a cell was missing, else none.
std::optional<float> FilledNoData(std::optional<float> noData, bool missing);

/// @brief Sets each cell of `target` to `sample` at the cell's centre; cells where it gives NaN
/// hold `noData`, or NaN without one, and `target` gets `FilledNoData`.
/// @param target The raster to fill, on its own grid.
/// @param noData The no-data value of what is sampled, if any.
/// @param sample Called with the world x and y of each cell centre; returns NaN where missing.
template <typename Sample>
void SampleAtCentres(Raster<float>& target, std::optional<float> noData, Sample&& sample) {
    const float fill = noData.value_or(std::numeric_limits<float>::quiet_NaN());
    bool missing = false;
    for (std::size_t row = 0; row < target.Height(); ++row) {
        for (std::size_t col = 0; col < target.Width(); ++col) {
            const Vec2d centre = target.CellCenter(col, row);
            const float value = sample(centre.x, centre.y);
            missing = missing || std::isnan(value);
            target(col, row) = std::isnan(value) ? fill : value;
        }
    }
    target.SetNoData(FilledNoData(noData, missing));
}

}  // namespace terraingen
