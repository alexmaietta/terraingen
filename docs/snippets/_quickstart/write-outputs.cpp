// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Write the Heights, Craters and Boulders to Files for the Quickstart
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, QuickstartWriteOutputs) {
    InOutputFolder();
    TerrainGenerator terrain;
    terrain.source = std::make_shared<FlatSource>(257, 257, 0.5);
    Fractal fractal = Fractal::Fbm(1, 80.0, 2.0);
    fractal.octaves = 5;
    const auto craters = std::make_shared<CraterLayer>();
    terrain.layers = {std::make_shared<NoiseLayer>(fractal), craters,
                      std::make_shared<BoulderLayer>(BoulderLayerOptions{}, craters)};
    const Raster<float> heights = Evaluate(terrain);
    // --8<-- [start:write_outputs]
    WriteGeoTiff("terrain.tif", heights, terrain);
    const RasterGrid grid = RasterGrid::FromRaster(heights);
    WriteCraterList(PlacedCraters(terrain, grid).craters, "craters.csv", ListFormat::Csv);
    WriteBoulderList(PlacedBoulders(terrain, grid).boulders, "boulders.csv", ListFormat::Csv);
    // --8<-- [end:write_outputs]
    for (const char* name : {"terrain.tif", "craters.csv", "boulders.csv"}) {
        EXPECT_TRUE(std::filesystem::exists(name)) << name;
    }
}

}  // namespace
}  // namespace terraingen
