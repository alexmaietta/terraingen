#include "terraingen/raster/grid.h"

#include <gtest/gtest.h>

#include <cmath>

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

TEST(RasterGrid, MeasuresSpacingAndAreaOnASphere) {
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    const RasterGrid whole{360, 180, GeoTransform::NorthUp(-180.0, 90.0, 1.0)};
    constexpr double kPi = 3.14159265358979323846;
    EXPECT_NEAR(whole.SurfaceArea(moon) / (4.0 * kPi * 1737400.0 * 1737400.0), 1.0, 1e-12);
    EXPECT_NEAR(whole.Spacing(moon), moon.UnitLength(), 1e-9);
    const RasterGrid band{10, 10, GeoTransform::NorthUp(0.0, 10.0, 1.0)};
    EXPECT_NEAR(band.SurfaceArea(moon),
                1737400.0 * 1737400.0 * 10.0 * kPi / 180.0 * std::sin(10.0 * kPi / 180.0), 1e-3);
    const RasterGrid plane{4, 2, GeoTransform::NorthUp(0.0, 0.0, 3.0)};
    EXPECT_EQ(plane.SurfaceArea({}), plane.Area());
    EXPECT_EQ(plane.Spacing({}), plane.PixelSize());
}
