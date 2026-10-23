// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/flat_source.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>

using terraingen::FlatSource;
using terraingen::GeoTransform;

TEST(FlatSource, CoversAGridCenteredOnTheOrigin) {
    const FlatSource source(10, 6, 2.0);
    const auto grid = source.Grid();
    EXPECT_EQ(grid.width, 10u);
    EXPECT_EQ(grid.height, 6u);
    EXPECT_EQ(grid.transform, GeoTransform::NorthUp(-10.0, 6.0, 2.0));
    EXPECT_EQ(source.ReferencePoint().x, 0.0);
    EXPECT_EQ(source.ReferencePoint().y, 0.0);
}

TEST(FlatSource, ResolvesNothing) {
    const FlatSource source(10, 6, 2.0);
    EXPECT_EQ(source.ResolutionAt(0.0, 0.0), std::numeric_limits<double>::infinity());
    EXPECT_EQ(source.ResolutionAt(1e6, -1e6), std::numeric_limits<double>::infinity());
}

TEST(FlatSource, FillsAnyGridWithItsElevation) {
    const FlatSource source(10, 6, 2.0, -3.5f);
    terraingen::RasterGrid tile{3, 2, GeoTransform::NorthUp(500.0, 500.0, 2.0)};
    auto heights = tile.MakeRaster(1.0f);
    source.Fill(heights);
    for (const float value : heights.Values()) {
        EXPECT_EQ(value, -3.5f);
    }
    EXPECT_EQ(source.HeightAt(1e6, -3.0, 0.5), -3.5f);
}

TEST(FlatSource, RejectsInvalidGrids) {
    EXPECT_THROW(FlatSource(0, 4, 1.0), std::invalid_argument);
    EXPECT_THROW(FlatSource(4, 0, 1.0), std::invalid_argument);
    EXPECT_THROW(FlatSource(4, 4, 0.0), std::invalid_argument);
    EXPECT_THROW(FlatSource(4, 4, std::numeric_limits<double>::infinity()), std::invalid_argument);
    EXPECT_THROW(FlatSource(4, 4, 1.0, std::numeric_limits<float>::quiet_NaN()),
                 std::invalid_argument);
}

TEST(FlatSource, CentersItsGridWhereAsked) {
    const FlatSource source(4, 2, 0.5, 1.0f, {30.0, -10.0});
    EXPECT_EQ(source.Grid().transform, GeoTransform::NorthUp(29.0, -9.5, 0.5));
    EXPECT_THROW(FlatSource(4, 2, 0.5, 1.0f, {std::numeric_limits<double>::quiet_NaN(), 0.0}),
                 std::invalid_argument);
}

TEST(FlatSource, TakesItsCellSizeInMetersOnASurface) {
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    const FlatSource site(4, 2, 30.0, 0.0f, {10.0, -20.0}, moon);
    const FlatSource degrees(4, 2, 30.0 / moon.UnitLength(), 0.0f, {10.0, -20.0});
    EXPECT_EQ(site.Grid().transform, degrees.Grid().transform);
    const FlatSource plane(4, 2, 0.5, 1.0f, {30.0, -10.0}, terraingen::Surface{});
    EXPECT_EQ(plane.Grid().transform, GeoTransform::NorthUp(29.0, -9.5, 0.5));
    EXPECT_THROW(FlatSource(4, 2, 0.0, 0.0f, {}, moon), std::invalid_argument);
}
