/// @file
/// @brief Parallel loops over a raster's cells: every cell, or the windows around features.
#pragma once

#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/arithmetic.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/parallel.h"
#include "terraingen/util/surface.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace terraingen {

/// A block of a raster's cells: columns [`colBegin`, `colEnd`) and rows [`rowBegin`,
/// `rowEnd`).
struct PixelWindow {
    std::size_t colBegin = 0;  ///< First column.
    std::size_t colEnd = 0;    ///< One past the last column.
    std::size_t rowBegin = 0;  ///< First row.
    std::size_t rowEnd = 0;    ///< One past the last row.

    /// Whether the window holds no cell, either range being empty.
    bool Empty() const {
        return colBegin >= colEnd || rowBegin >= rowEnd;
    }

    bool operator==(const PixelWindow&) const = default;
};

/// The possibly empty window of a grid's pixels whose extent meets a world-coordinate box,
/// clamped to the grid.
/// @throws std::domain_error if the grid's transform is not invertible.
PixelWindow PixelWindowIn(const RasterGrid& grid, const Bounds2d& box);

/// `PixelWindowIn` the raster's grid.
PixelWindow PixelWindowIn(const Raster<float>& raster, const Bounds2d& box);

/// The non-empty windows of a grid (in `surface`'s coordinates) covering a box: one on a plane,
/// up to three on a sphere, none off the grid.
/// @param box The box: on a sphere a longitude-latitude box, which is also taken a turn either way
/// so a grid running past the antimeridian is covered on both sides.
/// @throws std::domain_error if the grid's transform is not invertible.
std::vector<PixelWindow> PixelWindowsIn(const RasterGrid& grid, const Bounds2d& box,
                                        const Surface& surface);

/// `PixelWindowsIn` the raster's grid.
std::vector<PixelWindow> PixelWindowsIn(const Raster<float>& raster, const Bounds2d& box,
                                        const Surface& surface);

/// Rows per band of `ForEachFeatureCell`, the unit of work handed to a thread.
inline constexpr std::size_t kBandRows = 32;

/// One cell `ForEachFeatureCell` visits.
struct FeatureCell {
    std::size_t feature = 0;  ///< Index of the feature.
    std::size_t window = 0;   ///< Index of the window among the feature's.
    std::size_t col = 0;      ///< Column of the cell.
    std::size_t row = 0;      ///< Row of the cell.
    Vec2d centre{};           ///< World coordinates of the cell's centre.
};

/// Visits the cells of each feature's windows, feature by feature, on several threads.
///
/// Each band of `kBandRows` rows is one task and visits the features in order, so every cell sees
/// them in the same order whatever `threads` is. No-data cells are skipped.
/// @param threads Threads to split the bands over; 0 for every hardware thread.
/// @param featureWindows `featureWindows(n)` gives the windows of feature n, as a
/// `std::vector<PixelWindow>`
/// (`PixelWindowsIn`); windows of one feature must not overlap.
/// @param visit `visit(cell, at)` with `cell` the value to read and update and `at` its
/// `FeatureCell`.
template <typename T, typename FeatureWindows, typename Visit>
void ForEachFeatureCell(Raster<T>& raster, unsigned threads, std::size_t featureCount,
                        const FeatureWindows& featureWindows, const Visit& visit) {
    const std::size_t bands = CeilDiv(raster.Height(), kBandRows);
    ParallelFor(bands, threads, [&](std::size_t band) {
        const std::size_t bandBegin = band * kBandRows;
        const std::size_t bandEnd = std::min(raster.Height(), bandBegin + kBandRows);
        for (std::size_t n = 0; n < featureCount; ++n) {
            const std::vector<PixelWindow>& windows = featureWindows(n);
            for (std::size_t w = 0; w < windows.size(); ++w) {
                const PixelWindow& window = windows[w];
                const std::size_t rowEnd = std::min(bandEnd, window.rowEnd);
                for (std::size_t row = std::max(bandBegin, window.rowBegin); row < rowEnd; ++row) {
                    for (std::size_t col = window.colBegin; col < window.colEnd; ++col) {
                        T& cell = raster(col, row);
                        if (!raster.IsNoData(cell)) {
                            visit(cell, FeatureCell{n, w, col, row, raster.CellCenter(col, row)});
                        }
                    }
                }
            }
        }
    });
}

/// Sets each cell of a raster holding data to a function of its height and centre, rows
/// over several threads.
/// @param threads Threads to split the rows over; 0 for every hardware thread.
/// @param map `map(height, centre)` returning the cell's new height as a `float`.
template <typename Map> void MapCells(Raster<float>& heights, unsigned threads, const Map& map) {
    ParallelFor(heights.Height(), threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            float& cell = heights(col, row);
            if (!heights.IsNoData(cell)) {
                cell = map(cell, heights.CellCenter(col, row));
            }
        }
    });
}

}  // namespace terraingen
