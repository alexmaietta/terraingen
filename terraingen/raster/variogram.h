/// @file
/// @brief The variogram of a raster's heights and its log-log fit, the deviogram.
#pragma once

#include "terraingen/raster/raster.h"

#include <cstddef>
#include <utility>

namespace terraingen {

/// Whether cell (`col`, `row`) of `heights` holds a finite value that is not no-data.
bool HasData(const Raster<float>& heights, std::size_t col, std::size_t row);

/// The mean squared difference of cells `lag` apart along rows and columns where both hold data,
/// and how many pairs gave it.
std::pair<double, std::size_t> Variogram(const Raster<float>& heights, std::size_t lag);

/// A least-squares line through log2 RMS height difference over log2 lag in cells.
struct DeviogramFit {
    int lags = 0;            ///< Lags the line was fitted over; the slope needs at least 2.
    double slope = 0.0;      ///< The Hurst exponent H, unclamped.
    double intercept = 0.0;  ///< log2 of the RMS height difference at a lag of one cell.
};

/// The deviogram of `heights` (the square root of its `Variogram`) fitted at lags of 1, 2, 4 and
/// 8 cells, stopping at the first lag with no pairs or no relief.
DeviogramFit FitDeviogram(const Raster<float>& heights);

}  // namespace terraingen
