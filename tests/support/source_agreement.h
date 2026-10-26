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
#include <vector>

namespace terraingen::testing {

/// Checks that points, three tiles and the whole of `grid` (at least 8 by 6 cells) agree to the
/// bit at the grid's own spacing, and the whole on one thread with it on every; points outside the
/// data are NaN where the cells hold no-data. `HeightsAt` the cell centers and `Fill` into a buffer
/// give the points to the bit.
inline void ExpectPointsTilesAndGridAgree(const Source& source, const RasterGrid& grid) {
    const double spacing = grid.PixelSize();
    Raster<float> whole = grid.MakeRaster();
    source.Fill(whole, spacing);
    Raster<float> serial = grid.MakeRaster();
    source.Fill(serial, spacing, 1);
    for (std::size_t i = 0; i < whole.Size(); ++i) {
        ASSERT_TRUE(SameBits(serial.Values()[i], whole.Values()[i])) << "one thread, cell " << i;
    }
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
    std::vector<Vec2d> centers;
    for (std::size_t row = 0; row < whole.Height(); ++row) {
        for (std::size_t col = 0; col < whole.Width(); ++col) {
            centers.push_back(whole.CellCenter(col, row));
        }
    }
    std::vector<float> batch(centers.size());
    source.HeightsAt(Span<const Vec2d>(centers), Span<float>(batch), spacing);
    std::vector<float> buffer(centers.size());
    source.Fill(grid, Span<float>(buffer), spacing, 0);
    for (std::size_t i = 0; i < centers.size(); ++i) {
        const float point = source.HeightAt(centers[i].x, centers[i].y, spacing);
        const float cell = whole.Values()[i];
        ASSERT_TRUE(SameBits(point, cell) || (std::isnan(point) && whole.IsNoData(cell)))
            << "cell " << i;
        ASSERT_TRUE(SameBits(batch[i], point) || (std::isnan(batch[i]) && std::isnan(point)))
            << "batch, cell " << i;
        ASSERT_TRUE(SameBits(buffer[i], point) || (std::isnan(buffer[i]) && std::isnan(point)))
            << "buffer, cell " << i;
    }
}

}  // namespace terraingen::testing
