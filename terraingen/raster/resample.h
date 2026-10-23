/// @file
/// @brief Resampling and interpolation of rasters: nearest, bilinear, cubic, Lanczos and B-spline.
#pragma once

#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>

namespace terraingen {

/// How `Resample` interpolates between source cells.
enum class ResampleMethod {
    Nearest,   ///< The value of the nearest cell center.
    Bilinear,  ///< Linear in each axis over the four surrounding cells.
    Cubic,     ///< Keys cubic convolution (a = -0.5) over the sixteen surrounding cells.
    /// The Catmull-Rom spline over the sixteen surrounding cells: the same kernel as `Cubic`, Keys
    /// cubic convolution with a = -0.5, to the bit.
    CatmullRom,
    /// The Lanczos kernel sinc(x) sinc(x / 3) over the 36 surrounding cells, its weights
    /// normalized to sum to 1 along each axis.
    Lanczos3,
    /// The cubic B-spline over the sixteen surrounding cells: smooth, but it does not pass through
    /// the cells, so it smooths them.
    CubicBSpline,
};

/// Cells either side of a point that `method` reads along each axis: 1 for nearest and bilinear,
/// 2 for the cubic kernels, 3 for Lanczos.
int KernelRadius(ResampleMethod method);

/// The weight of `method`'s kernel at an offset of `x` cells, before any normalization: 1 at 0 and
/// 0 at the other whole offsets for the interpolating kernels.
double KernelWeight(ResampleMethod method, double x);

/// The weights of the 2 `KernelRadius` cells around a point `fraction` of the way from cell 0 to
/// cell 1, for cells -radius + 1 to radius, as the interpolation applies them; nearest is not a
/// kernel and gives none.
/// @param fraction In [0, 1).
std::array<double, 6> KernelWeights(ResampleMethod method, double fraction);

/// Values on a lattice interpolated at lattice position (`u`, `v`), measured in cells from the
/// center of cell (0, 0), with `method`: cells are read as `value(col, row)` with whole `col`
/// and `row` as doubles, and taps of zero weight are skipped. Nothing where a tap with weight
/// holds a value that `missing(value)` says has no data.
/// @tparam Value Callable as `float(double col, double row)`.
/// @tparam Missing Callable as `bool(float value)`.
template <typename Value, typename Missing>
std::optional<float> InterpolateLattice(double u, double v, ResampleMethod method,
                                        const Value& value, const Missing& missing) {
    if (method == ResampleMethod::Nearest) {
        const float nearest = value(std::round(u), std::round(v));
        if (missing(nearest)) {
            return std::nullopt;
        }
        return nearest;
    }
    const double u0 = std::floor(u);
    const double v0 = std::floor(v);
    const std::array<double, 6> wxs = KernelWeights(method, u - u0);
    const std::array<double, 6> wys = KernelWeights(method, v - v0);
    const int radius = KernelRadius(method);
    double sum = 0.0;
    for (int j = 1 - radius; j <= radius; ++j) {
        const double wy = wys[static_cast<std::size_t>(j + radius - 1)];
        for (int i = 1 - radius; i <= radius; ++i) {
            const double wx = wxs[static_cast<std::size_t>(i + radius - 1)];
            if (wx * wy == 0.0) {
                continue;
            }
            const float cell = value(u0 + i, v0 + j);
            if (missing(cell)) {
                return std::nullopt;
            }
            sum += wx * wy * static_cast<double>(cell);
        }
    }
    return static_cast<float>(sum);
}

/// A raster resampled to square cells over the same extent, origin and axis directions, on the
/// `ResampledGrid`, keeping `raster`'s no-data value (NaN cells without one).
///
/// Each output cell center is interpolated in the source, with samples beyond the edge taken from
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

/// Where a raster that is a window of a larger one, such as a window read from a file, lies in
/// it.
struct RasterPlacement {
    RasterGrid whole{};   ///< The larger raster's grid.
    std::size_t col = 0;  ///< The window's first column in it.
    std::size_t row = 0;  ///< The window's first row in it.

    /// A raster placed in itself.
    static RasterPlacement Whole(const Raster<float>& raster) {
        return {RasterGrid::FromRaster(raster), 0, 0};
    }
};

/// `SampleRaster` of the larger raster `window` is placed in, read from `window` alone: the same
/// bits wherever every cell interpolated lies in `window` or beyond the larger raster's edge
/// where `window` reaches it; NaN outside the larger raster.
/// @param window The window; not empty and not rotated.
/// @throws std::invalid_argument for an empty or rotated window.
/// @throws std::domain_error if the larger raster's transform is not invertible.
float SampleRasterWindow(const Raster<float>& window, const RasterPlacement& placement, double x,
                         double y, ResampleMethod method);

/// The cells `cells` of `Resample` of the larger raster `window` is placed in, read from `window`
/// alone, on their grid of the resampled lattice: the same bits wherever every cell interpolated
/// lies in `window` or beyond the larger raster's edge where `window` reaches it.
/// @param cells A non-empty window of `ResampledGrid(placement.whole, pixelSize)`.
/// @throws std::invalid_argument as `Resample`, or for `cells` empty or beyond the resampled
/// grid.
Raster<float> ResampleWindow(const Raster<float>& window, const RasterPlacement& placement,
                             double pixelSize, ResampleMethod method, const PixelWindow& cells);

/// A raster resampled onto a non-empty grid, each cell `SampleRaster` at its center.
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

/// Sets each cell of `target` to `sample` at the cell's center; cells where it gives NaN
/// hold `noData`, or NaN without one, and `target` gets `FilledNoData`.
/// @tparam Sample Callable as `float(double x, double y)` with world coordinates; returns NaN
/// where missing.
template <typename Sample>
void SampleAtCenters(Raster<float>& target, std::optional<float> noData, Sample&& sample) {
    const float fill = noData.value_or(std::numeric_limits<float>::quiet_NaN());
    bool missing = false;
    for (std::size_t row = 0; row < target.Height(); ++row) {
        for (std::size_t col = 0; col < target.Width(); ++col) {
            const Vec2d center = target.CellCenter(col, row);
            const float value = sample(center.x, center.y);
            missing = missing || std::isnan(value);
            target(col, row) = std::isnan(value) ? fill : value;
        }
    }
    target.SetNoData(FilledNoData(noData, missing));
}

}  // namespace terraingen
