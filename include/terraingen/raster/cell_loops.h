// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Parallel loops over a raster's cells: every cell, or the windows around features.
#pragma once

#include "terraingen/geometry/geometry.h"
#include "terraingen/geometry/surface.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/parallel.h"

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

    /// Memberwise equality.
    bool operator==(const PixelWindow& other) const {
        return colBegin == other.colBegin && colEnd == other.colEnd && rowBegin == other.rowBegin &&
               rowEnd == other.rowEnd;
    }
    bool operator!=(const PixelWindow& other) const {
        return !(*this == other);
    }
};

/// The possibly empty window of a grid's pixels whose extent meets a world-coordinate box,
/// clamped to the grid.
/// @throws std::domain_error if the grid's transform is not invertible.
PixelWindow PixelWindowIn(const RasterGrid& grid, const Bounds2d& box);

/// The window of every cell of a grid.
inline PixelWindow WholeWindow(const RasterGrid& grid) {
    return {0, grid.width, 0, grid.height};
}

/// The grid of the cells `cells` of `grid`, on its lattice: its transform starts at the window's
/// first cell, and is `grid`'s own when that is the first cell of `grid`.
RasterGrid Subgrid(const RasterGrid& grid, const PixelWindow& cells);

/// Calls `visit(tile)` for each tile of up to `side` x `side` cells of a `width` x `height` grid,
/// row by row of tiles, the tiles split over threads.
/// @param threads Threads to split the tiles over; 0 for every hardware thread.
template <typename Visit>
void ForEachTile(std::size_t width, std::size_t height, std::size_t side, unsigned threads,
                 const Visit& visit) {
    const std::size_t tilesX = CeilDiv(width, side);
    ParallelFor(tilesX * CeilDiv(height, side), threads, [&](std::size_t tile) {
        const std::size_t colBegin = (tile % tilesX) * side;
        const std::size_t rowBegin = (tile / tilesX) * side;
        visit(PixelWindow{colBegin, std::min(colBegin + side, width), rowBegin,
                          std::min(rowBegin + side, height)});
    });
}

/// The non-empty windows of a grid (in `surface`'s coordinates) covering a box: one on a plane,
/// up to three on a sphere, none off the grid.
/// @param box The box: on a sphere a longitude-latitude box, which is also taken a turn either way
/// so a grid running past the antimeridian is covered on both sides.
/// @throws std::domain_error if the grid's transform is not invertible.
std::vector<PixelWindow> PixelWindowsIn(const RasterGrid& grid, const Bounds2d& box,
                                        const Surface& surface);

/// One cell `ForEachFeatureCell` visits.
struct FeatureCell {
    std::size_t feature = 0;  ///< Index of the feature.
    std::size_t window = 0;   ///< Index of the window among the feature's.
    std::size_t col = 0;      ///< Column of the cell.
    std::size_t row = 0;      ///< Row of the cell.
    Vec2d center{};           ///< World coordinates of the cell's center.
};

/// Visits the cells holding data of each feature's windows, feature by feature, in bands of rows
/// over several threads, so every cell sees the features in the same order whatever `threads` is.
/// @param threads Threads to split the bands over; 0 for every hardware thread.
/// @param featureWindows `featureWindows(n)` gives feature n's non-overlapping windows as a
/// `std::vector<PixelWindow>` (`PixelWindowsIn`).
/// @param visit `visit(cell, at)`, `cell` the value to read and update and `at` its `FeatureCell`.
template <typename T, typename FeatureWindows, typename Visit>
void ForEachFeatureCell(Raster<T>& raster, unsigned threads, std::size_t featureCount,
                        const FeatureWindows& featureWindows, const Visit& visit) {
    constexpr std::size_t kBandRows = 32;
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

/// `ForEachFeatureCell` over the cells of each feature's box (`PixelWindowsIn` on `surface`).
template <typename T, typename Visit>
void ForEachFeatureCell(Raster<T>& raster, unsigned threads, const std::vector<Bounds2d>& boxes,
                        const Surface& surface, const Visit& visit) {
    const RasterGrid grid{raster.Width(), raster.Height(), raster.Transform()};
    std::vector<std::vector<PixelWindow>> windows;
    windows.reserve(boxes.size());
    for (const Bounds2d& box : boxes) {
        windows.push_back(PixelWindowsIn(grid, box, surface));
    }
    ForEachFeatureCell(
        raster, threads, boxes.size(),
        [&](std::size_t n) -> const std::vector<PixelWindow>& { return windows[n]; }, visit);
}

/// Calls `visit(cell, center)` for every cell of `raster`, `cell` the value to read and update
/// and `center` its world coordinates, rows over several threads.
/// @param threads Threads to split the rows over; 0 for every hardware thread.
template <typename T, typename Visit>
void ForEachCell(Raster<T>& raster, unsigned threads, const Visit& visit) {
    ParallelFor(raster.Height(), threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < raster.Width(); ++col) {
            visit(raster(col, row), raster.CellCenter(col, row));
        }
    });
}

/// Sets each cell of `raster` holding data to `map(value, center)` of its value and center, rows
/// over several threads; a fresh raster holds data in every cell.
/// @param threads Threads to split the rows over; 0 for every hardware thread.
template <typename T, typename Map>
void MapCells(Raster<T>& raster, unsigned threads, const Map& map) {
    ForEachCell(raster, threads, [&](T& cell, const Vec2d& center) {
        if (!raster.IsNoData(cell)) {
            cell = static_cast<T>(map(cell, center));
        }
    });
}

}  // namespace terraingen
