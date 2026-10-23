/// @file
/// @brief The deviogram of a raster's heights: the log-log fit of its variogram.
#pragma once

#include "terraingen/raster/raster.h"

#include <cstddef>

namespace terraingen {

/// Whether cell (`col`, `row`) of `heights` holds a finite value that is not no-data.
bool HasData(const Raster<float>& heights, std::size_t col, std::size_t row);

/// A least-squares line through log2 RMS height difference over log2 lag in cells.
struct DeviogramFit {
    int lags = 0;            ///< Lags the line was fitted over; the slope needs at least 2.
    double slope = 0.0;      ///< The Hurst exponent H, unclamped.
    double intercept = 0.0;  ///< log2 of the RMS height difference at a lag of one cell.
    double cellRms = 0.0;  ///< The measured RMS difference at one cell; 0 without pairs or relief.
};

/// The deviogram of `heights` (the root mean squared difference of cells a lag apart along rows
/// and columns) fitted at lags of 1, 2, 4 and 8 cells, stopping at the first lag with no pairs or
/// no relief.
DeviogramFit FitDeviogram(const Raster<float>& heights);

}  // namespace terraingen
