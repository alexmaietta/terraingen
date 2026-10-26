// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Terrain reports: statistics of evaluated grids, layer timings and run details as JSON.
#pragma once

#include "terraingen/distribution/size_frequency.h"
#include "terraingen/layers/boulders/boulder_placer.h"
#include "terraingen/layers/craters/crater_placer.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/surface.h"
#include "terraingen/util/version.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace terraingen {

/// The craters listed in one diameter bin [`minDiameter`, `maxDiameter`) and the count a target
/// size-frequency distribution expects there.
struct CraterBin {
    double minDiameter = 0.0;  ///< Smallest diameter in meters, a power of 2.
    double maxDiameter = 0.0;  ///< Largest diameter in meters, twice `minDiameter`.
    std::size_t count = 0;     ///< Craters listed in the bin.
    /// Craters the target expects over the grid's area in the bin; NaN without a target, for a
    /// production-rate target or for a bin outside the target's valid range.
    double expected = std::numeric_limits<double>::quiet_NaN();
};

/// Statistics of one evaluated grid; heights and slopes are over cells with data, NaN without
/// any.
struct GridReport {
    RasterGrid grid{};  ///< The grid: its size, pixel size and extent.
    double heightMin = std::numeric_limits<double>::quiet_NaN();   ///< Lowest height in meters.
    double heightMax = std::numeric_limits<double>::quiet_NaN();   ///< Highest height in meters.
    double heightMean = std::numeric_limits<double>::quiet_NaN();  ///< Mean height in meters.
    /// Population standard deviation of the heights in meters.
    double heightStd = std::numeric_limits<double>::quiet_NaN();
    /// Mean slope in degrees, from central differences in the surface's local metric.
    double slopeMeanDeg = std::numeric_limits<double>::quiet_NaN();
    double slopeMedianDeg = std::numeric_limits<double>::quiet_NaN();  ///< Median slope, degrees.
    /// 95th percentile slope in degrees, interpolated between the nearest ranks.
    double slopeP95Deg = std::numeric_limits<double>::quiet_NaN();
    /// The listed craters per power-of-2 diameter bin, smallest first, from the bin of the
    /// smallest crater to that of the largest; empty without craters.
    std::vector<CraterBin> craterBins{};
    std::size_t boulderCount = 0;       ///< Boulders listed.
    std::size_t boulderModelCount = 0;  ///< Boulders placed as their own 3D models.
    /// Summed boulder footprints, pi d^2 / 4 each, over the grid's area on its surface; boulders
    /// that overlap or reach past the grid are counted whole.
    double boulderCover = 0.0;
};

/// The time one layer took over every grid.
struct LayerTiming {
    std::string type;      ///< The layer's type, such as "craters".
    double seconds = 0.0;  ///< Wall-clock seconds.
};

/// A generated terrain's report: its grids' statistics, layer timings and how it was run.
struct TerrainReport {
    std::vector<GridReport> grids{};     ///< One per evaluated grid.
    std::vector<LayerTiming> layers{};   ///< One per layer, in order.
    std::string backend{};               ///< Where the heights were evaluated, such as "cpu".
    unsigned threads = 0;                ///< Threads requested; 0 for every hardware thread.
    std::vector<std::uint32_t> seeds{};  ///< The seeds the terrain was generated with.
    double seconds = 0.0;                ///< Wall-clock seconds the generation took.
    std::string version = TERRAINGEN_FULL_VERSION_STRING;  ///< The library version.
};

/// Statistics of `heights`, evaluated over a grid on `surface`, and of the features over it.
///
/// Slopes come from central differences (one-sided at edges and beside no-data cells) of the
/// heights against the east and north offsets in meters between cell centers on `surface`;
/// cells without data, or without a neighbor with data along a grid axis, are skipped.
/// @param craters The craters listed over the grid, or null.
/// @param boulders The boulders listed over the grid, or null.
/// @param craterTarget The crater density the bins' expected counts come from, or null.
/// @param threads Threads for the slopes; 0 for every hardware thread. The result does not
/// depend on it.
GridReport ReportGrid(const Raster<float>& heights, const Surface& surface,
                      const CraterFeatures* craters, const BoulderFeatures* boulders,
                      const SizeFrequencyDistribution* craterTarget, unsigned threads = 0);

/// `report` as JSON text, with NaN written as null and doubles written exactly.
/// @param indent Spaces per level; negative for one line.
std::string ReportToJson(const TerrainReport& report, int indent = 2);

}  // namespace terraingen
