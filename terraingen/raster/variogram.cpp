// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/raster/variogram.h"

#include <cmath>
#include <utility>

namespace terraingen {

bool HasData(const Raster<float>& heights, std::size_t col, std::size_t row) {
    const float value = heights(col, row);
    return std::isfinite(value) && !heights.IsNoData(value);
}

namespace {

/// The longest lag, in cells, the deviogram is fitted over.
constexpr std::size_t kMaxVariogramLag = 8;

/// The mean squared difference of cells `lag` apart along rows and columns where both hold data,
/// and how many pairs gave it.
std::pair<double, std::size_t> Variogram(const Raster<float>& heights, std::size_t lag) {
    double sum = 0.0;
    std::size_t count = 0;
    const auto add = [&](std::size_t col, std::size_t row, std::size_t col2, std::size_t row2) {
        if (HasData(heights, col, row) && HasData(heights, col2, row2)) {
            const double difference =
                static_cast<double>(heights(col2, row2)) - static_cast<double>(heights(col, row));
            sum += difference * difference;
            ++count;
        }
    };
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            if (col + lag < heights.Width()) {
                add(col, row, col + lag, row);
            }
            if (row + lag < heights.Height()) {
                add(col, row, col, row + lag);
            }
        }
    }
    return {count > 0 ? sum / static_cast<double>(count) : 0.0, count};
}

}  // namespace

DeviogramFit FitDeviogram(const Raster<float>& heights) {
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXX = 0.0;
    double sumXY = 0.0;
    int lags = 0;
    double cellRms = 0.0;
    for (std::size_t lag = 1; lag <= kMaxVariogramLag; lag *= 2) {
        const auto [variogram, pairs] = Variogram(heights, lag);
        if (pairs == 0 || !(variogram > 0.0)) {
            break;
        }
        const double x = std::log2(static_cast<double>(lag));
        if (lag == 1) {
            cellRms = std::sqrt(variogram);
        }
        const double y = 0.5 * std::log2(variogram);
        sumX += x;
        sumY += y;
        sumXX += x * x;
        sumXY += x * y;
        ++lags;
    }
    DeviogramFit fit;
    fit.lags = lags;
    fit.cellRms = cellRms;
    if (lags >= 2) {
        fit.slope = (lags * sumXY - sumX * sumY) / (lags * sumXX - sumX * sumX);
        fit.intercept = (sumY - fit.slope * sumX) / lags;
    }
    return fit;
}

}  // namespace terraingen
