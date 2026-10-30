// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Minimum, maximum, mean and standard deviation of a raster's valid cells.
#pragma once

#include "terraingen/raster/raster.h"

#include <cmath>
#include <cstddef>
#include <limits>

namespace terraingen {

/// Summary statistics of a raster's valid cells.
struct RasterStats {
    double min = std::numeric_limits<double>::quiet_NaN();     ///< NaN if no cell is valid.
    double max = std::numeric_limits<double>::quiet_NaN();     ///< NaN if no cell is valid.
    double mean = std::numeric_limits<double>::quiet_NaN();    ///< NaN if no cell is valid.
    double stddev = std::numeric_limits<double>::quiet_NaN();  ///< Population standard deviation.
    std::size_t validCount = 0;  ///< Cells that are finite and not the nodata value.
};

/// Statistics over the cells that are finite and not the raster's no-data value; NaN with a
/// count of 0 when no cell is valid.
/// @tparam T Cell type, convertible to `double`.
template <typename T> RasterStats ComputeStats(const Raster<T>& raster) {
    RasterStats stats;
    const auto forEachValid = [&](const auto& visit) {
        for (const T value : raster.Values()) {
            if (!raster.IsMissing(value)) {
                visit(static_cast<double>(value));
            }
        }
    };
    double sum = 0.0;
    forEachValid([&](double v) {
        stats.min = stats.validCount == 0 ? v : std::fmin(stats.min, v);
        stats.max = stats.validCount == 0 ? v : std::fmax(stats.max, v);
        sum += v;
        ++stats.validCount;
    });
    if (stats.validCount == 0) {
        return stats;
    }
    const auto count = static_cast<double>(stats.validCount);
    stats.mean = sum / count;
    double squares = 0.0;
    forEachValid([&](double v) { squares += (v - stats.mean) * (v - stats.mean); });
    stats.stddev = std::sqrt(squares / count);
    return stats;
}

}  // namespace terraingen
