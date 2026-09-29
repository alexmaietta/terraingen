#include "terraingen/core/geo_transform.h"

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

TEST(GeoTransform, NorthUpMapsCornersAndCentres) {
    const GeoTransform t = GeoTransform::NorthUp(1000.0, 2000.0, 5.0);

    const Vec2d corner = t.PixelToWorld(0.0, 0.0);
    EXPECT_DOUBLE_EQ(corner.x, 1000.0);
    EXPECT_DOUBLE_EQ(corner.y, 2000.0);

    const Vec2d centre = t.PixelToWorld(0.5, 0.5);
    EXPECT_DOUBLE_EQ(centre.x, 1002.5);
    EXPECT_DOUBLE_EQ(centre.y, 1997.5);

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
