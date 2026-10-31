// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Vary Crater Density With a Map
///
/// A density map sets a crater layer's density point by point, such as from a crater count map
/// of a site. A scale map multiplies the layer's own density, while count and areal fraction maps
/// measure craters in a diameter range. Here a map rising from 0.1 times in the west to 3 times
/// in the east crowds the craters eastward.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, VaryCraterDensityWithAMap) {
    const std::size_t cells = 100;
    Raster<float> scale(RasterGrid{cells, cells, GeoTransform::NorthUp(-200.0, 200.0, 4.0)});
    for (std::size_t row = 0; row < cells; ++row) {
        for (std::size_t column = 0; column < cells; ++column) {
            scale(column, row) = 0.1f + 2.9f * (static_cast<float>(column) + 0.5f) / cells;
        }
    }
    // --8<-- [start:vary_crater_density_with_a_map]
    DensityMap densityMap;
    densityMap.field = std::make_shared<RasterField>(scale);
    densityMap.measure = DensityMeasure::Scale;  // times the layer's own density
    CraterLayerOptions options;
    options.seed = 5;
    options.generation.minDiameter = 4.0;
    options.generation.maxDiameter = 60.0;
    options.densityMap = densityMap;
    const CraterLayer craters(DefaultCraterModels(), options);
    const CraterList list = craters.Craters({-200.0, -200.0, 200.0, 200.0});
    // --8<-- [end:vary_crater_density_with_a_map]
    std::size_t east = 0;
    for (const double x : list.X()) {
        east += x >= 0.0 ? 1 : 0;
    }
    EXPECT_GT(east, 2 * (list.Size() - east));
}

}  // namespace
}  // namespace terraingen
