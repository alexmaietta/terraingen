// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/terrain_base.h"

#include "terraingen/sources/raster_source.h"

#include <gtest/gtest.h>

#include <memory>

using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::RasterGrid;
using terraingen::RasterSource;
using terraingen::Surface;
using terraingen::TerrainBase;

namespace {

std::shared_ptr<RasterSource> Dem() {
    Raster<float> heights(RasterGrid{4, 4, GeoTransform::NorthUp(500000.0, 4000000.0, 10.0)}, 1.0f);
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
