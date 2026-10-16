/// @file
/// @brief Gaussian smoothing of rasters that skips no-data cells.
#pragma once

#include "terraingen/raster/raster.h"

namespace terraingen {

/// A raster smoothed by a separable Gaussian kernel truncated at three standard deviations,
/// keeping `raster`'s grid and no-data value.
///
/// No-data cells keep their value and do not contribute; the kernel is renormalized over valid
/// cells, so edges are not pulled towards zero.
/// @param sigmaPixels The standard deviation in cells; finite and >= 0, with 0 giving a copy.
/// @throws std::invalid_argument unless `sigmaPixels` is finite and not negative.
Raster<float> GaussianBlur(const Raster<float>& raster, double sigmaPixels);

/// `GaussianBlur` with separate standard deviations along each axis, as on a
/// longitude-latitude grid where cells are narrower east to west.
/// @param sigmaColumns The standard deviation along rows, in cells; finite and >= 0.
/// @param sigmaRows The standard deviation along columns, in cells; finite and >= 0. Both 0
/// give a copy.
/// @throws std::invalid_argument unless both are finite and not negative.
Raster<float> GaussianBlur(const Raster<float>& raster, double sigmaColumns, double sigmaRows);

}  // namespace terraingen
