// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Minimum, maximum, mean and standard deviation of a raster's valid cells, and slope
/// statistics of a raster of heights.
#pragma once

#include "terraingen/geometry/surface.h"
#include "terraingen/raster/raster.h"

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
RasterStats ComputeStats(const Raster<float>& raster);

/// Slope statistics of a raster of heights, in degrees.
struct SlopeStats {
    double mean = std::numeric_limits<double>::quiet_NaN();    ///< NaN if no slope is taken.
    double median = std::numeric_limits<double>::quiet_NaN();  ///< NaN if no slope is taken.
    /// The 95th percentile, interpolated between the nearest ranks; NaN if no slope is taken.
    double p95 = std::numeric_limits<double>::quiet_NaN();
    std::size_t validCount = 0;  ///< Cells a slope is taken at.
};

/// Slopes of `heights` on `surface` from central differences (one-sided at edges and beside
/// no-data cells) against the east and north offsets in meters between cell centers; cells
/// without data, or without a neighbor with data along a grid axis, are skipped.
/// @param threads Threads to use; 0 for every hardware thread. The result does not depend on it.
SlopeStats ComputeSlopeStats(const Raster<float>& heights, const Surface& surface,
                             unsigned threads = 0);

}  // namespace terraingen
