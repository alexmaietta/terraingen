// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/raster/variogram.h"

#include <cmath>

namespace terraingen {

bool HasData(const Raster<float>& heights, std::size_t col, std::size_t row) {
    const float value = heights(col, row);
    return std::isfinite(value) && !heights.IsNoData(value);
}

DeviogramFit FitDeviogram(const Raster<float>& heights) {
    constexpr std::size_t kMaxLag = 8;
    return FitLagLine(kMaxLag, [&](std::size_t lag) {
        double sum = 0.0;
        std::size_t count = 0;
        ForEachLagPair(heights, lag, [&](double a, double b) {
            sum += (b - a) * (b - a);
            ++count;
        });
        return count > 0 ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
    });
}

}  // namespace terraingen
