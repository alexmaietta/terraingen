// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Checks that a source's points, tiles and whole grid agree, for source tests.
#pragma once

#include "support/raster_equality.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/grid.h"
#include "terraingen/sources/source.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>

namespace terraingen::testing {

/// Checks that points, three tiles and the whole of `grid` (at least 8 by 6 cells) agree to the
/// bit at the grid's own spacing; points outside the data are NaN where the cells hold no-data.
inline void ExpectPointsTilesAndGridAgree(const Source& source, const RasterGrid& grid) {
    const double spacing = grid.PixelSize();
    Raster<float> whole = grid.MakeRaster();
    source.Fill(whole, spacing);
    const PixelWindow tiles[] = {
        {0, 7, 0, 5}, {7, grid.width, 0, 5}, {0, grid.width, 5, grid.height}};
    for (const PixelWindow& cells : tiles) {
        Raster<float> tile = Subgrid(grid, cells).MakeRaster();
        source.Fill(tile, spacing);
        for (std::size_t row = 0; row < tile.Height(); ++row) {
            for (std::size_t col = 0; col < tile.Width(); ++col) {
                ASSERT_TRUE(
                    SameBits(tile(col, row), whole(cells.colBegin + col, cells.rowBegin + row)));
            }
        }
    }
    for (std::size_t row = 0; row < whole.Height(); ++row) {
        for (std::size_t col = 0; col < whole.Width(); ++col) {
            const Vec2d center = whole.CellCenter(col, row);
            const float point = source.HeightAt(center.x, center.y, spacing);
            const float cell = whole(col, row);
            ASSERT_TRUE(SameBits(point, cell) || (std::isnan(point) && whole.IsNoData(cell)))
                << col << "," << row;
        }
    }
}

}  // namespace terraingen::testing
