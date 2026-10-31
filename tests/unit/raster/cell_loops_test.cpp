// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/grid.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

using terraingen::PixelWindow;
using terraingen::Raster;
using terraingen::RasterGrid;
using terraingen::Surface;

namespace {

/// A `width` x `height` raster of 1-unit cells with its top-left corner at (0, `height`).
Raster<float> Grid(std::size_t width, std::size_t height) {
    Raster<float> raster(
        RasterGrid{width, height,
                   terraingen::GeoTransform::NorthUp(0.0, static_cast<double>(height), 1.0)},
        0.0f);
    return raster;
}

/// A whole-globe raster of 10 degree cells, longitudes from -180.
Raster<float> Globe() {
    Raster<float> raster(RasterGrid{36, 18, terraingen::GeoTransform::NorthUp(-180.0, 90.0, 10.0)},
                         0.0f);
    return raster;
}

}  // namespace

TEST(PixelWindowIn, CoversTheBoxAndClampsToTheRaster) {
    const terraingen::RasterGrid grid = Grid(10, 8).Grid();
    EXPECT_EQ(terraingen::PixelWindowIn(grid, {2.5, 3.5, 4.5, 5.5}), (PixelWindow{2, 5, 2, 5}));
    EXPECT_EQ(terraingen::PixelWindowIn(grid, {-5.0, -5.0, 50.0, 50.0}),
              (PixelWindow{0, 10, 0, 8}));
    EXPECT_TRUE(terraingen::PixelWindowIn(grid, {20.0, 20.0, 30.0, 30.0}).Empty());
}

TEST(Subgrid, StartsAtTheWindowsFirstCellOnTheSameLattice) {
    const auto grid = Grid(10, 8).Grid();
    EXPECT_EQ(terraingen::WholeWindow(grid), (PixelWindow{0, 10, 0, 8}));
    EXPECT_EQ(terraingen::Subgrid(grid, terraingen::WholeWindow(grid)), grid);
    const terraingen::RasterGrid part = terraingen::Subgrid(grid, {2, 5, 3, 7});
    EXPECT_EQ(part.width, 3u);
    EXPECT_EQ(part.height, 4u);
    EXPECT_EQ(part.transform, terraingen::GeoTransform::NorthUp(2.0, 5.0, 1.0));
    EXPECT_EQ(terraingen::Subgrid(grid, {4, 4, 0, 8}).width, 0u);
}

TEST(PixelWindowsIn, GivesOneWindowOnAPlaneAndWrapsLongitudeOnASphere) {
    const RasterGrid plane = Grid(10, 8).Grid();
    EXPECT_EQ(terraingen::PixelWindowsIn(plane, {1.0, 1.0, 2.0, 2.0}, Surface()).size(), 1u);
    EXPECT_TRUE(terraingen::PixelWindowsIn(plane, {20.0, 20.0, 30.0, 30.0}, Surface()).empty());
    const RasterGrid globe = Globe().Grid();
    const Surface moon = Surface::Sphere(1737400.0);
    const auto across = terraingen::PixelWindowsIn(globe, {170.0, -5.0, 190.0, 5.0}, moon);
    ASSERT_EQ(across.size(), 2u) << "the box runs past the antimeridian";
    EXPECT_EQ(across[0].colBegin, 0u);
    EXPECT_EQ(across[1].colEnd, 36u);
}

TEST(ForEachFeatureCell, VisitsEachFeaturesCellsInOrderOnAnyNumberOfThreads) {
    for (const unsigned threads : {1u, 3u}) {
        Raster<float> raster = Grid(40, 70);
        raster.SetNoData(-1.0f);
        raster(5, 5) = -1.0f;
        const std::vector<terraingen::Bounds2d> boxes{
            {0.0, 30.0, 10.0, 70.0}, {5.0, 0.0, 15.0, 40.0}, {30.0, 68.0, 32.0, 70.0}};
        terraingen::ForEachFeatureCell(
            raster, threads, boxes, Surface{}, [&](float& cell, const terraingen::FeatureCell& at) {
                cell = cell * 10.0f + static_cast<float>(at.feature + 1);
                EXPECT_EQ(at.center.x, static_cast<double>(at.col) + 0.5);
                EXPECT_TRUE(Surface{}.Contains(boxes[at.feature], at.center));
            });
        EXPECT_EQ(raster(0, 0), 1.0f);
        EXPECT_EQ(raster(5, 5), -1.0f) << "no data is skipped";
        EXPECT_EQ(raster(6, 35), 12.0f) << "the first feature, then the second";
        EXPECT_EQ(raster(12, 60), 2.0f);
        EXPECT_EQ(raster(31, 1), 3.0f) << "a feature in a tile of its own";
        EXPECT_EQ(raster(20, 20), 0.0f);
    }
}

TEST(MapCells, MapsEveryCellHoldingData) {
    Raster<float> raster = Grid(5, 4);
    raster.SetNoData(std::numeric_limits<float>::quiet_NaN());
    raster(1, 1) = std::nanf("");
    terraingen::MapCells(raster, 2, [](float height, const terraingen::Vec2d& center) {
        return height + static_cast<float>(center.x);
    });
    EXPECT_EQ(raster(0, 0), 0.5f);
    EXPECT_EQ(raster(4, 3), 4.5f);
    EXPECT_TRUE(std::isnan(raster(1, 1)));
}

TEST(ResolvedSpacing, RaisesTheSpacingToTheRastersOwn) {
    const Raster<float> raster = Grid(4, 4);
    EXPECT_EQ(terraingen::ResolvedSpacing(raster.Grid(), 0.0, Surface()), 1.0);
    EXPECT_EQ(terraingen::ResolvedSpacing(raster.Grid(), 3.0, Surface()), 3.0);
    const Surface moon = Surface::Sphere(1737400.0);
    EXPECT_DOUBLE_EQ(terraingen::ResolvedSpacing(Globe().Grid(), 0.0, moon),
                     10.0 * moon.UnitLength());
    Raster<float> flat(RasterGrid{2, 2, {0.0, 0.0, 0.0, 0.0, 0.0, 0.0}}, 0.0f);
    EXPECT_THROW(terraingen::ResolvedSpacing(flat.Grid(), 0.0, Surface()), std::domain_error);
}

TEST(ForEachTile, CoversEveryCellOnceInTilesOfTheSide) {
    std::vector<int> visits(10 * 7, 0);
    terraingen::ForEachTile(10, 7, 4, 0, [&](const PixelWindow& tile) {
        EXPECT_LE(tile.colEnd - tile.colBegin, 4u);
        EXPECT_LE(tile.rowEnd - tile.rowBegin, 4u);
        for (std::size_t row = tile.rowBegin; row < tile.rowEnd; ++row) {
            for (std::size_t col = tile.colBegin; col < tile.colEnd; ++col) {
                ++visits[row * 10 + col];
            }
        }
    });
    for (const int count : visits) {
        EXPECT_EQ(count, 1);
    }
    bool called = false;
    terraingen::ForEachTile(0, 5, 4, 1, [&](const PixelWindow&) { called = true; });
    EXPECT_FALSE(called);
}

TEST(ForEachTileIn, VisitsTheTilesAWindowMeetsClippedToIt) {
    const PixelWindow window{3, 11, 2, 5};
    std::vector<PixelWindow> cells;
    std::vector<std::size_t> tiles;
    terraingen::ForEachTileIn(window, 4, 3,
                              [&](std::size_t tx, std::size_t ty, const PixelWindow& tile) {
                                  tiles.push_back(ty * 10 + tx);
                                  cells.push_back(tile);
                              });
    EXPECT_EQ(tiles, (std::vector<std::size_t>{0, 1, 2, 10, 11, 12}));
    EXPECT_EQ(cells.front(), (PixelWindow{3, 4, 2, 3}));
    EXPECT_EQ(cells[1], (PixelWindow{4, 8, 2, 3}));
    EXPECT_EQ(cells.back(), (PixelWindow{8, 11, 3, 5}));
    EXPECT_EQ(terraingen::TileWindow(1, 0, 4, 3, window), (PixelWindow{4, 8, 2, 3}));
}

TEST(PixelWindow, CountsItsColumnsAndRows) {
    EXPECT_EQ((PixelWindow{3, 11, 2, 5}).Width(), 8u);
    EXPECT_EQ((PixelWindow{3, 11, 2, 5}).Height(), 3u);
    EXPECT_EQ((PixelWindow{5, 2, 4, 4}).Width(), 0u);
    EXPECT_EQ((PixelWindow{5, 2, 4, 4}).Height(), 0u);
}

TEST(ForEachFeatureCell, VisitsTheCellsOfEachFeaturesBox) {
    Raster<double> raster(RasterGrid{10, 10, terraingen::GeoTransform::NorthUp(0.0, 10.0, 1.0)},
                          0.0);
    const std::vector<terraingen::Bounds2d> boxes{{0.0, 0.0, 2.0, 2.0}, {1.0, 1.0, 3.0, 3.0}};
    terraingen::ForEachFeatureCell(raster, 2, boxes, Surface{},
                                   [](double& cell, const terraingen::FeatureCell& at) {
                                       cell += static_cast<double>(at.feature + 1);
                                   });
    EXPECT_EQ(raster(0, 9), 1.0) << "only in the first box";
    EXPECT_EQ(raster(1, 8), 3.0) << "in both";
    EXPECT_EQ(raster(2, 7), 2.0) << "only in the second";
    EXPECT_EQ(raster(5, 5), 0.0);
}

TEST(MapCells, SetsEveryCellOfAFreshRasterOfAnyType) {
    Raster<double> raster(RasterGrid{4, 3, terraingen::GeoTransform::NorthUp(0.0, 3.0, 1.0)}, 7.0);
    terraingen::MapCells(
        raster, 2, [](double value, const terraingen::Vec2d& center) { return value + center.x; });
    EXPECT_EQ(raster(0, 0), 7.5);
    EXPECT_EQ(raster(3, 2), 10.5);
}
