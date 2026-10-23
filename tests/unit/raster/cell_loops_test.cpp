#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/grid.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

using terraingen::PixelWindow;
using terraingen::Raster;
using terraingen::Surface;

namespace {

/// A `width` x `height` raster of 1-unit cells with its top-left corner at (0, `height`).
Raster<float> Grid(std::size_t width, std::size_t height) {
    Raster<float> raster(width, height, 0.0f);
    raster.SetTransform(terraingen::GeoTransform::NorthUp(0.0, static_cast<double>(height), 1.0));
    return raster;
}

/// A whole-globe raster of 10 degree cells, longitudes from -180.
Raster<float> Globe() {
    Raster<float> raster(36, 18, 0.0f);
    raster.SetTransform(terraingen::GeoTransform::NorthUp(-180.0, 90.0, 10.0));
    return raster;
}

}  // namespace

TEST(PixelWindowIn, CoversTheBoxAndClampsToTheRaster) {
    const Raster<float> raster = Grid(10, 8);
    EXPECT_EQ(terraingen::PixelWindowIn(raster, {2.5, 3.5, 4.5, 5.5}), (PixelWindow{2, 5, 2, 5}));
    EXPECT_EQ(terraingen::PixelWindowIn(raster, {-5.0, -5.0, 50.0, 50.0}),
              (PixelWindow{0, 10, 0, 8}));
    EXPECT_TRUE(terraingen::PixelWindowIn(raster, {20.0, 20.0, 30.0, 30.0}).Empty());
}

TEST(Subgrid, StartsAtTheWindowsFirstCellOnTheSameLattice) {
    const auto grid = terraingen::RasterGrid::FromRaster(Grid(10, 8));
    EXPECT_EQ(terraingen::WholeWindow(grid), (PixelWindow{0, 10, 0, 8}));
    EXPECT_EQ(terraingen::Subgrid(grid, terraingen::WholeWindow(grid)), grid);
    const terraingen::RasterGrid part = terraingen::Subgrid(grid, {2, 5, 3, 7});
    EXPECT_EQ(part.width, 3u);
    EXPECT_EQ(part.height, 4u);
    EXPECT_EQ(part.transform, terraingen::GeoTransform::NorthUp(2.0, 5.0, 1.0));
    EXPECT_EQ(terraingen::Subgrid(grid, {4, 4, 0, 8}).width, 0u);
}

TEST(PixelWindowsIn, GivesOneWindowOnAPlaneAndWrapsLongitudeOnASphere) {
    const Raster<float> plane = Grid(10, 8);
    EXPECT_EQ(terraingen::PixelWindowsIn(plane, {1.0, 1.0, 2.0, 2.0}, Surface()).size(), 1u);
    EXPECT_TRUE(terraingen::PixelWindowsIn(plane, {20.0, 20.0, 30.0, 30.0}, Surface()).empty());
    const Raster<float> globe = Globe();
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
        const std::vector<std::vector<PixelWindow>> windows{{{0, 10, 0, 40}},
                                                            {{5, 15, 30, 70}, {30, 32, 0, 2}}};
        terraingen::ForEachFeatureCell(
            raster, threads, windows.size(),
            [&](std::size_t n) -> const std::vector<PixelWindow>& { return windows[n]; },
            [&](float& cell, const terraingen::FeatureCell& at) {
                cell = cell * 10.0f + static_cast<float>(at.feature + 1);
                EXPECT_EQ(at.center.x, static_cast<double>(at.col) + 0.5);
                EXPECT_EQ(windows[at.feature][at.window].colBegin <= at.col, true);
            });
        EXPECT_EQ(raster(0, 0), 1.0f);
        EXPECT_EQ(raster(5, 5), -1.0f) << "no data is skipped";
        EXPECT_EQ(raster(6, 35), 12.0f) << "the first feature, then the second";
        EXPECT_EQ(raster(12, 60), 2.0f);
        EXPECT_EQ(raster(31, 1), 2.0f);
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
    EXPECT_EQ(terraingen::ResolvedSpacing(raster, 0.0, Surface()), 1.0);
    EXPECT_EQ(terraingen::ResolvedSpacing(raster, 3.0, Surface()), 3.0);
    const Surface moon = Surface::Sphere(1737400.0);
    EXPECT_DOUBLE_EQ(terraingen::ResolvedSpacing(Globe(), 0.0, moon), 10.0 * moon.UnitLength());
    Raster<float> flat(2, 2, 0.0f);
    flat.SetTransform({0.0, 0.0, 0.0, 0.0, 0.0, 0.0});
    EXPECT_THROW(terraingen::ResolvedSpacing(flat, 0.0, Surface()), std::domain_error);
}
