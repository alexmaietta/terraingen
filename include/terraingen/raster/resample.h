// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Interpolation of rasters: nearest, bilinear, cubic, Lanczos and B-spline.
#pragma once

#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

namespace terraingen {

/// How `Resample` interpolates between source cells.
enum class ResampleMethod {
    Nearest,   ///< The value of the nearest cell center.
    Bilinear,  ///< Linear in each axis over the four surrounding cells.
    /// Keys cubic convolution (a = -0.5), the Catmull-Rom spline, over the sixteen surrounding
    /// cells.
    Cubic,
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
                                        const Value& value, const Missing& missing);

/// `InterpolateLattice` of a kernel of `radius` from its taps' weights, as `KernelWeights` gives
/// them: `wxs` along the row from column `u0` and `wys` down the column from row `v0`, the cell
/// at or before the point along each axis.
template <typename Value, typename Missing>
std::optional<float> InterpolateLatticeWeights(double u0, double v0,
                                               const std::array<double, 6>& wxs,
                                               const std::array<double, 6>& wys, int radius,
                                               const Value& value, const Missing& missing) {
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
    return InterpolateLatticeWeights(u0, v0, KernelWeights(method, u - u0),
                                     KernelWeights(method, v - v0), KernelRadius(method), value,
                                     missing);
}

/// A raster interpolated at world position (`x`, `y`), with cells beyond the edge read from the
/// nearest edge cell; NaN outside the raster's extent or where a contributing cell is no-data.
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

/// The no-data value of a raster filled from a source with no-data value `noData`, whose
/// missing cells hold `noData` or, without one, NaN: `noData` if set; otherwise NaN if a cell
/// was `missing`, else none.
std::optional<float> FilledNoData(std::optional<float> noData, bool missing);

}  // namespace terraingen
