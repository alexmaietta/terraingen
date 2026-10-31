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
#include <utility>
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

    /// The number of columns; 0 for none.
    std::size_t Width() const {
        return colEnd > colBegin ? colEnd - colBegin : 0;
    }

    /// The number of rows; 0 for none.
    std::size_t Height() const {
        return rowEnd > rowBegin ? rowEnd - rowBegin : 0;
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

/// The cells of tile (`tx`, `ty`) of `tileWidth` x `tileHeight` cells that lie in `window`.
inline PixelWindow TileWindow(std::size_t tx, std::size_t ty, std::size_t tileWidth,
                              std::size_t tileHeight, const PixelWindow& window) {
    return {
        std::max(window.colBegin, tx * tileWidth), std::min(window.colEnd, (tx + 1) * tileWidth),
        std::max(window.rowBegin, ty * tileHeight), std::min(window.rowEnd, (ty + 1) * tileHeight)};
}

/// Calls `visit(tx, ty, cells)` for each tile (`tx`, `ty`) of `tileWidth` x `tileHeight` cells
/// that a non-empty `window` meets, row by row of tiles, with its cells in the window
/// (`TileWindow`).
template <typename Visit>
void ForEachTileIn(const PixelWindow& window, std::size_t tileWidth, std::size_t tileHeight,
                   const Visit& visit) {
    for (std::size_t ty = window.rowBegin / tileHeight; ty <= (window.rowEnd - 1) / tileHeight;
         ++ty) {
        for (std::size_t tx = window.colBegin / tileWidth; tx <= (window.colEnd - 1) / tileWidth;
             ++tx) {
            visit(tx, ty, TileWindow(tx, ty, tileWidth, tileHeight, window));
        }
    }
}

/// Calls `visit(tile)` for each tile of up to `side` x `side` cells of a `width` x `height` grid,
/// row by row of tiles, the tiles split over threads.
/// @param threads Threads to split the tiles over; 0 for every hardware thread.
template <typename Visit>
void ForEachTile(std::size_t width, std::size_t height, std::size_t side, unsigned threads,
                 const Visit& visit) {
    const std::size_t tilesX = CeilDiv(width, side);
    ParallelFor(tilesX * CeilDiv(height, side), threads, [&](std::size_t tile) {
        visit(TileWindow(tile % tilesX, tile / tilesX, side, side, {0, width, 0, height}));
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
    std::size_t col = 0;      ///< Column of the cell.
    std::size_t row = 0;      ///< Row of the cell.
    Vec2d center{};           ///< World coordinates of the cell's center.
};

/// Side in cells of the tiles `ForEachFeatureCell` sorts features into.
inline constexpr std::size_t kFeatureTileCells = 32;

/// Visits the cells holding data of each feature's box (`PixelWindowsIn` on `surface`), feature by
/// feature: the boxes are sorted into tiles of `kFeatureTileCells` cells, which run over several
/// threads and visit their features in order, so every cell sees the features in the same order
/// whatever `threads` is.
/// @param threads Threads to split the tiles over; 0 for every hardware thread.
/// @param visit `visit(cell, at)`, `cell` the value to read and update and `at` its `FeatureCell`.
template <typename T, typename Visit>
void ForEachFeatureCell(Raster<T>& raster, unsigned threads, const std::vector<Bounds2d>& boxes,
                        const Surface& surface, const Visit& visit) {
    constexpr std::size_t kSide = kFeatureTileCells;
    const RasterGrid& grid = raster.Grid();
    const std::size_t tilesX = CeilDiv(raster.Width(), kSide);
    std::vector<std::vector<std::pair<std::size_t, PixelWindow>>> tiles(
        tilesX * CeilDiv(raster.Height(), kSide));
    for (std::size_t n = 0; n < boxes.size(); ++n) {
        for (const PixelWindow& window : PixelWindowsIn(grid, boxes[n], surface)) {
            ForEachTileIn(window, kSide, kSide,
                          [&](std::size_t tx, std::size_t ty, const PixelWindow& cells) {
                              tiles[ty * tilesX + tx].emplace_back(n, cells);
                          });
        }
    }
    ParallelFor(tiles.size(), threads, [&](std::size_t tile) {
        for (const auto& [feature, cells] : tiles[tile]) {
            for (std::size_t row = cells.rowBegin; row < cells.rowEnd; ++row) {
                for (std::size_t col = cells.colBegin; col < cells.colEnd; ++col) {
                    T& cell = raster(col, row);
                    if (!raster.IsNoData(cell)) {
                        visit(cell, FeatureCell{feature, col, row, raster.CellCenter(col, row)});
                    }
                }
            }
        }
    });
}

/// Sets each cell of `raster` holding data to `map(value, center)`, rows over several threads.
/// @param threads Threads to split the rows over; 0 for every hardware thread.
template <typename T, typename Map>
void MapCells(Raster<T>& raster, unsigned threads, const Map& map) {
    ParallelFor(raster.Height(), threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < raster.Width(); ++col) {
            T& cell = raster(col, row);
            if (!raster.IsNoData(cell)) {
                cell = static_cast<T>(map(cell, raster.CellCenter(col, row)));
            }
        }
    });
}

}  // namespace terraingen
