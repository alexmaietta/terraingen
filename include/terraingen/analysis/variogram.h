// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Lag pairs of a raster's cells and log-log fits over lags, such as the deviogram.
#pragma once

#include "terraingen/raster/raster.h"

#include <cmath>
#include <cstddef>

namespace terraingen {

/// Calls `visit(a, b)` with the heights of every pair of cells `lag` apart along rows and columns
/// where both hold data.
template <typename Visit>
void ForEachLagPair(const Raster<float>& heights, std::size_t lag, const Visit& visit) {
    const auto pair = [&](std::size_t col, std::size_t row, std::size_t col2, std::size_t row2) {
        if (!heights.IsMissing(heights(col, row)) && !heights.IsMissing(heights(col2, row2))) {
            visit(static_cast<double>(heights(col, row)), static_cast<double>(heights(col2, row2)));
        }
    };
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            if (col + lag < heights.Width()) {
                pair(col, row, col + lag, row);
            }
            if (row + lag < heights.Height()) {
                pair(col, row, col, row + lag);
            }
        }
    }
}

/// A least-squares line through log2 of a height difference statistic over log2 lag in cells.
struct DeviogramFit {
    int lags = 0;            ///< Lags the line was fitted over; the slope needs at least 2.
    double slope = 0.0;      ///< The Hurst exponent H, unclamped.
    double intercept = 0.0;  ///< log2 of the statistic at a lag of one cell.
    double cellRms = 0.0;    ///< The statistic at one cell; 0 without pairs or relief.
};

/// The line through log2 `statistic(lag)` over log2 lag at lags of 1, 2, 4, ... up to `maxLag`
/// cells, stopping at the first lag where the statistic is not positive.
template <typename Statistic>
DeviogramFit FitLagLine(std::size_t maxLag, const Statistic& statistic) {
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXX = 0.0;
    double sumXY = 0.0;
    DeviogramFit fit;
    for (std::size_t lag = 1; lag <= maxLag; lag *= 2) {
        const double value = statistic(lag);
        if (!(value > 0.0)) {
            break;
        }
        fit.cellRms = lag == 1 ? value : fit.cellRms;
        const double x = std::log2(static_cast<double>(lag));
        const double y = std::log2(value);
        sumX += x;
        sumY += y;
        sumXX += x * x;
        sumXY += x * y;
        ++fit.lags;
    }
    if (fit.lags >= 2) {
        fit.slope = (fit.lags * sumXY - sumX * sumY) / (fit.lags * sumXX - sumX * sumX);
        fit.intercept = (sumY - fit.slope * sumX) / fit.lags;
    }
    return fit;
}

/// The deviogram of `heights`: `FitLagLine` of the root mean squared difference of lag pairs
/// (`ForEachLagPair`) at lags of 1, 2, 4 and 8 cells.
DeviogramFit FitDeviogram(const Raster<float>& heights);

}  // namespace terraingen
