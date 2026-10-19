/// @file
/// @brief Internal: the brightness statistics `RasterAlbedo` measures a map's roughness from.
#pragma once

#include "terraingen/raster/raster.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace terraingen {

/// The logarithm of the mean of a map's bands at each cell, row-major; NaN where a band has no
/// data or is not positive.
std::vector<double> LogBrightnessGrid(const std::vector<Raster<float>>& bands);

/// The structure function of a row-major grid at a lag of `lag` cells: the mean squared
/// difference of the values a lag apart along rows and columns, skipping non-finite values.
/// Empty when no pair was found or every pair is equal.
std::optional<double> StructureFunction(const std::vector<double>& values, std::size_t width,
                                        std::size_t height, std::size_t lag);

/// The least-squares slope of a line through points.
/// @param x The abscissae; at least two, not all equal.
/// @param y The ordinates, one per abscissa.
double FitSlope(const std::vector<double>& x, const std::vector<double>& y);

}  // namespace terraingen
