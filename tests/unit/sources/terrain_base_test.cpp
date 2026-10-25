// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/terrain_base.h"

#include "terraingen/sources/raster_source.h"

#include <gtest/gtest.h>

#include <memory>

using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::RasterSource;
using terraingen::Surface;
using terraingen::TerrainBase;

namespace {

std::shared_ptr<RasterSource> Dem() {
    Raster<float> heights(4, 4, 1.0f);
    heights.SetTransform(GeoTransform::NorthUp(500000.0, 4000000.0, 10.0));
    return std::make_shared<RasterSource>(heights);
}

/// The surface of `base`, to show what a Surface or a source converts to.
Surface SurfaceOf(const TerrainBase& base) {
    return base.surface;
}

}  // namespace

TEST(TerrainBase, IsAPlaneWithoutASourceByDefault) {
    const TerrainBase base;
    EXPECT_EQ(base.surface, Surface::Plane());
    EXPECT_EQ(base.source, nullptr);
    EXPECT_EQ(base.ReferencePoint().x, 0.0);
    EXPECT_EQ(base.ReferencePoint().y, 0.0);
}

TEST(TerrainBase, TakesASurfaceASourceOrBoth) {
    const Surface moon = Surface::Sphere(1737400.0);
    EXPECT_EQ(SurfaceOf(moon), moon);
    const std::shared_ptr<RasterSource> dem = Dem();
    const TerrainBase onPlane = dem;
    EXPECT_EQ(onPlane.surface, Surface::Plane());
    EXPECT_EQ(onPlane.source, dem);
    const TerrainBase both{moon, dem};
    EXPECT_EQ(both.surface, moon);
    EXPECT_EQ(both.source, dem);
}

TEST(TerrainBase, MeasuresNoiseFromItsSourcesReferencePoint) {
    const std::shared_ptr<RasterSource> dem = Dem();
    const TerrainBase base{Surface::Plane(), dem};
    EXPECT_EQ(base.ReferencePoint().x, dem->ReferencePoint().x);
    EXPECT_EQ(base.ReferencePoint().y, dem->ReferencePoint().y);
    EXPECT_NE(base.ReferencePoint().x, 0.0);
}
