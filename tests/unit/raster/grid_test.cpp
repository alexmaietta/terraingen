// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/raster/grid.h"

#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>

using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::RasterGrid;

TEST(ResolvedSpacing, RaisesARequestedSpacingToTheGrids) {
    const RasterGrid grid{4, 3, GeoTransform::NorthUp(0.0, 0.0, 2.0)};
    const terraingen::Surface plane;
    EXPECT_EQ(terraingen::ResolvedSpacing(grid, 0.0, plane), 2.0);
    EXPECT_EQ(terraingen::ResolvedSpacing(grid, 1.0, plane), 2.0);
    EXPECT_EQ(terraingen::ResolvedSpacing(grid, 5.0, plane), 5.0);
    const auto moon = terraingen::Surface::Sphere(1737400.0);
    const RasterGrid degrees{4, 3, GeoTransform::NorthUp(0.0, 0.0, 0.01)};
    EXPECT_EQ(terraingen::ResolvedSpacing(degrees, 0.0, moon), degrees.Spacing(moon));
}

TEST(RasterGrid, DescribesARastersCellsAndExtent) {
    Raster<float> raster(4, 3);
    raster.SetTransform(GeoTransform::NorthUp(100.0, 50.0, 2.5));
    const RasterGrid grid = RasterGrid::FromRaster(raster);
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
    EXPECT_EQ(RasterGrid::FromRaster(raster), grid);
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

TEST(ResampledGrid, KeepsTheExtentWithSquareCells) {
    const RasterGrid grid{10, 4, GeoTransform{100.0, 3.0, 0.0, 50.0, 0.0, -2.0}};
    const RasterGrid resampled = terraingen::ResampledGrid(grid, 1.0);
    EXPECT_EQ(resampled.width, 30u);
    EXPECT_EQ(resampled.height, 8u);
    EXPECT_EQ(resampled.transform, (GeoTransform{100.0, 1.0, 0.0, 50.0, 0.0, -1.0}));
    EXPECT_EQ(terraingen::ResampledGrid(grid, 1000.0).width, 1u);
    EXPECT_THROW(terraingen::ResampledGrid(grid, 0.0), std::invalid_argument);
    EXPECT_THROW(terraingen::ResampledGrid(grid, 1e-12), std::invalid_argument);
    const RasterGrid rotated{2, 2, GeoTransform{0.0, 1.0, 0.1, 0.0, 0.0, -1.0}};
    EXPECT_THROW(terraingen::ResampledGrid(rotated, 1.0), std::invalid_argument);
}

TEST(RasterGrid, SurfaceAreaIntegratesAnEllipsoid) {
    const double a = 3000.0;
    const double c = 1000.0;
    const terraingen::Surface spheroid = terraingen::Surface::Ellipsoid(a, a, c);
    const RasterGrid whole{360, 180, GeoTransform::NorthUp(-180.0, 90.0, 1.0)};
    const double e = std::sqrt(1.0 - c * c / (a * a));
    constexpr double kPi = 3.14159265358979323846;
    const double expected = 2.0 * kPi * a * a * (1.0 + (1.0 - e * e) / e * std::atanh(e));
    EXPECT_NEAR(whole.SurfaceArea(spheroid) / expected, 1.0, 1e-4);
    const RasterGrid north{360, 90, GeoTransform::NorthUp(-180.0, 90.0, 1.0)};
    EXPECT_NEAR(north.SurfaceArea(spheroid) / expected, 0.5, 1e-4);
    const RasterGrid empty{10, 0, GeoTransform::NorthUp(0.0, 10.0, 1.0)};
    EXPECT_EQ(empty.SurfaceArea(spheroid), 0.0);
}
