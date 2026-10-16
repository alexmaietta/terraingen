/// @file
/// @brief Resampling and interpolation of rasters: nearest, bilinear and cubic.
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

/// A raster resampled to square cells over the same extent, origin and axis directions, on the
/// `ResampledGrid`, keeping `raster`'s no-data value (NaN cells without one).
///
/// Each output cell centre is interpolated in the source, with samples beyond the edge taken from
/// the nearest edge cell. An output cell is no-data where any contributing cell is.
/// @param raster The source raster; not empty and not rotated.
/// @param pixelSize The new cell side in world units; positive and finite.
/// @throws std::invalid_argument for an empty or rotated raster, a non-positive or non-finite
/// `pixelSize`, or a result wider or taller than 2^31 cells.
Raster<float> Resample(const Raster<float>& raster, double pixelSize, ResampleMethod method);

/// A raster interpolated at world position (`x`, `y`), as `Resample` interpolates; NaN outside
/// the raster's extent or where a contributing cell is no-data.
/// @param raster The raster; not empty and not rotated.
/// @throws std::invalid_argument for an empty or rotated raster.
/// @throws std::domain_error if the raster's transform is not invertible.
float SampleRaster(const Raster<float>& raster, double x, double y, ResampleMethod method);

/// A raster resampled onto a non-empty grid, each cell `SampleRaster` at its centre.
///
/// Cells giving NaN hold `raster`'s no-data value, or NaN, which then becomes the no-data value
/// (`FilledNoData`).
/// @param raster The source raster; not empty and not rotated.
/// @throws std::invalid_argument for an empty or rotated raster, or an empty grid.
/// @throws std::domain_error if the raster's transform is not invertible.
Raster<float> ResampleTo(const Raster<float>& raster, const RasterGrid& grid,
                         ResampleMethod method);

/// The no-data value of a raster filled from a source with no-data value `noData`, whose
/// missing cells hold `noData` or, without one, NaN: `noData` if set; otherwise NaN if a cell
/// was `missing`, else none.
std::optional<float> FilledNoData(std::optional<float> noData, bool missing);

/// Sets each cell of `target` to `sample` at the cell's centre; cells where it gives NaN
/// hold `noData`, or NaN without one, and `target` gets `FilledNoData`.
/// @tparam Sample Callable as `float(double x, double y)` with world coordinates; returns NaN
/// where missing.
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
