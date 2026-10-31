// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/raster/geo_transform.h"

#include <gtest/gtest.h>

#include <array>
#include <stdexcept>

using terraingen::GeoTransform;
using terraingen::Vec2d;

TEST(GeoTransform, DefaultMatchesGdalDefault) {
    const std::array<double, 6> gdalDefault{0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    EXPECT_EQ(GeoTransform{}.ToGdal(), gdalDefault);
}

TEST(GeoTransform, GdalCoefficientsRoundTrip) {
    const std::array<double, 6> coefficients{500.0, 2.0, 0.25, -300.0, -0.5, -2.0};
    EXPECT_EQ(GeoTransform::FromGdal(coefficients).ToGdal(), coefficients);
}

TEST(GeoTransform, NorthUpMapsCornersAndCenters) {
    const GeoTransform t = GeoTransform::NorthUp(1000.0, 2000.0, 5.0);

    const Vec2d corner = t.PixelToWorld(0.0, 0.0);
    EXPECT_DOUBLE_EQ(corner.x, 1000.0);
    EXPECT_DOUBLE_EQ(corner.y, 2000.0);

    const Vec2d center = t.PixelToWorld(0.5, 0.5);
    EXPECT_DOUBLE_EQ(center.x, 1002.5);
    EXPECT_DOUBLE_EQ(center.y, 1997.5);

    const Vec2d farCorner = t.PixelToWorld(10.0, 4.0);
    EXPECT_DOUBLE_EQ(farCorner.x, 1050.0);
    EXPECT_DOUBLE_EQ(farCorner.y, 1980.0);
}

TEST(GeoTransform, WorldToPixelInvertsPixelToWorld) {
    const GeoTransform t = GeoTransform::FromGdal({500.0, 2.0, 0.25, -300.0, -0.5, -2.0});
    for (const Vec2d pixel : {Vec2d{0.0, 0.0}, Vec2d{3.5, 7.25}, Vec2d{-4.0, 12.0}}) {
        const Vec2d world = t.PixelToWorld(pixel.x, pixel.y);
        const Vec2d back = t.WorldToPixel(world.x, world.y);
        EXPECT_NEAR(back.x, pixel.x, 1e-12);
        EXPECT_NEAR(back.y, pixel.y, 1e-12);
    }
}

TEST(GeoTransform, WorldToPixelThrowsWhenNotInvertible) {
    const GeoTransform degenerate = GeoTransform::FromGdal({0.0, 1.0, 1.0, 0.0, 1.0, 1.0});
    EXPECT_THROW(degenerate.WorldToPixel(1.0, 1.0), std::domain_error);
}

TEST(GeoTransform, IsRotatedWhenEitherRotationTermIsSet) {
    EXPECT_FALSE(GeoTransform::NorthUp(0.0, 0.0, 1.0).IsRotated());
    EXPECT_TRUE(GeoTransform::FromGdal({0.0, 1.0, 0.5, 0.0, 0.0, -1.0}).IsRotated());
    EXPECT_TRUE(GeoTransform::FromGdal({0.0, 1.0, 0.0, 0.0, -0.5, -1.0}).IsRotated());
}

TEST(GeoTransform, EqualityComparesEveryCoefficient) {
    const std::array<double, 6> base{1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    EXPECT_EQ(GeoTransform::FromGdal(base), GeoTransform::FromGdal(base));
    for (std::size_t i = 0; i < base.size(); ++i) {
        std::array<double, 6> changed = base;
        changed[i] += 1.0;
        EXPECT_NE(GeoTransform::FromGdal(base), GeoTransform::FromGdal(changed))
            << "coefficient " << i;
    }
}

TEST(GeoTransform, MeasuresTheSidesOfItsCells) {
    const GeoTransform oblong{0.0, 2.0, 0.0, 0.0, 0.0, -5.0};
    EXPECT_EQ(oblong.CellWidth(), 2.0);
    EXPECT_EQ(oblong.CellHeight(), 5.0);
    const GeoTransform turned{0.0, 3.0, -4.0, 0.0, 4.0, 3.0};
    EXPECT_DOUBLE_EQ(turned.CellWidth(), 5.0);
    EXPECT_DOUBLE_EQ(turned.CellHeight(), 5.0);
}
