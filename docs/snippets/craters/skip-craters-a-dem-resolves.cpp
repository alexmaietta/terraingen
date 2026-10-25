// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Skip the Craters a DEM Already Resolves
///
/// A DEM already holds the craters large enough for its posts to resolve, so a crater layer on it
/// should add only the smaller ones. Given the DEM as its source, a layer drops generated craters
/// at least the source resolves pixels of its cells across, 4 by default. Here a DEM with 8 m
/// posts gets only craters under 32 m.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, SkipCratersADemResolves) {
    Raster<float> posts(151, 151);
    posts.SetTransform(GeoTransform::NorthUp(-604.0, 604.0, 8.0));
    const auto dem = std::make_shared<RasterSource>(posts);
    // --8<-- [start:skip_craters_a_dem_resolves]
    CraterLayerOptions options;
    options.seed = 5;
    options.generation.dMin = 4.0;
    options.generation.dMax = 200.0;
    options.sourceResolvesPixels = 4.0;  // DEM cells across; unset keeps every crater
    const CraterLayer craters(DefaultCraterModels(), options, TerrainBase{Surface::Plane(), dem});
    const CraterList list = craters.Craters({-300.0, -300.0, 300.0, 300.0});
    // --8<-- [end:skip_craters_a_dem_resolves]
    ASSERT_GT(list.Size(), 0u);
    EXPECT_LT(*std::max_element(list.Diameters().begin(), list.Diameters().end()), 32.0);
}

}  // namespace
}  // namespace terraingen
