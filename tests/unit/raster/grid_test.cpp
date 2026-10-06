#include "terraingen/raster/grid.h"

#include <gtest/gtest.h>

using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::RasterGrid;

TEST(RasterGrid, DescribesARastersCellsAndExtent) {
    Raster<float> raster(4, 3);
    raster.SetTransform(GeoTransform::NorthUp(100.0, 50.0, 2.5));
    const RasterGrid grid = RasterGrid::Of(raster);
    EXPECT_EQ(grid.width, 4u);
    EXPECT_EQ(grid.height, 3u);
    EXPECT_EQ(grid.transform, raster.Transform());
    EXPECT_DOUBLE_EQ(grid.PixelSize(), 2.5);
    EXPECT_DOUBLE_EQ(grid.Area(), 10.0 * 7.5);
    const auto bounds = grid.Bounds();
    EXPECT_EQ(bounds.minX, 100.0);
    EXPECT_EQ(bounds.maxX, 110.0);
    EXPECT_EQ(bounds.minY, 42.5);
    EXPECT_EQ(bounds.maxY, 50.0);
}

TEST(RasterGrid, MakesRastersOnTheGrid) {
    const RasterGrid grid{3, 2, GeoTransform::NorthUp(-1.0, 1.0, 1.0)};
    const Raster<float> raster = grid.MakeRaster(7.0f);
    EXPECT_EQ(RasterGrid::Of(raster), grid);
    for (const float value : raster.Values()) {
        EXPECT_EQ(value, 7.0f);
    }
    EXPECT_EQ(grid.MakeRaster()(2, 1), 0.0f);
}
