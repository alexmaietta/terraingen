/// @file
/// @brief Minimum, maximum and mean of a raster's valid cells.
#pragma once

#include "terraingen/raster/raster.h"

#include <cmath>
#include <cstddef>
#include <limits>

namespace terraingen {

/// @brief Summary statistics of a raster's valid cells.
struct RasterStats {
    double min = std::numeric_limits<double>::quiet_NaN();   ///< NaN if no cell is valid.
    double max = std::numeric_limits<double>::quiet_NaN();   ///< NaN if no cell is valid.
    double mean = std::numeric_limits<double>::quiet_NaN();  ///< NaN if no cell is valid.
    std::size_t validCount = 0;  ///< Cells that are finite and not the nodata value.
};

/// @brief Statistics over the cells that are finite and not the raster's no-data value.
/// @tparam T Cell type, convertible to `double`.
/// @param raster The raster.
/// @return The statistics; NaN with a count of 0 when no cell is valid.
template <typename T> RasterStats ComputeStats(const Raster<T>& raster) {
    RasterStats stats;
    double sum = 0.0;
    for (const T value : raster.Values()) {
        const auto v = static_cast<double>(value);
        if (!std::isfinite(v) || (raster.NoData() && value == *raster.NoData())) {
            continue;
        }
        if (stats.validCount == 0) {
            stats.min = v;
            stats.max = v;
        } else {
            stats.min = std::fmin(stats.min, v);
            stats.max = std::fmax(stats.max, v);
        }
        sum += v;
        ++stats.validCount;
    }
    if (stats.validCount > 0) {
        stats.mean = sum / static_cast<double>(stats.validCount);
    }
    return stats;
}

}  // namespace terraingen
