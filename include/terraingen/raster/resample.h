// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Interpolation of rasters: nearest, bilinear, cubic, Lanczos and B-spline.
#pragma once

#include "terraingen/raster/raster.h"

namespace terraingen {

/// How a raster is interpolated between its cells.
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

/// A raster interpolated at world position (`x`, `y`), with cells beyond the edge read from the
/// nearest edge cell; NaN outside the raster's extent or where a contributing cell is no-data.
/// @param raster The raster; not empty and not rotated.
/// @throws std::invalid_argument for an empty or rotated raster.
/// @throws std::domain_error if the raster's transform is not invertible.
float SampleRaster(const Raster<float>& raster, double x, double y, ResampleMethod method);

}  // namespace terraingen
