// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Upscale an Albedo Map With Fractal Detail
///
/// An albedo map, such as a normal albedo mosaic from orbit, starts an albedo as `RasterAlbedo`.
/// It is interpolated to the grid and given detail below its cells, fractal noise that continues
/// the map's own measured roughness to finer scales. Here a 10 m map read from a GeoTIFF is
/// evaluated at 0.5 m. Outside the map the albedo is NaN.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, UpscaleAnAlbedoMap) {
    const std::filesystem::path path = OutputFolder() / "albedo.tif";
    SyntheticAlbedoOptions noise;
    noise.valueNoise = {Fractal::Fbm(7, 120.0, 0.3)};
    Albedo stand;
    stand.source = std::make_shared<SyntheticAlbedo>(noise);
    WriteGeoTiff(path,
                 EvaluateAlbedo(stand, RasterGrid::Centered({0.0, 0.0}, {200.0, 200.0}, 10.0), {}));
    // --8<-- [start:upscale_an_albedo_map]
    const GeoTiff map = ReadGeoTiff(path);  // 10 m cells
    RasterAlbedoOptions options;
    options.method = ResampleMethod::Cubic;
    options.detail.seed = 1;
    options.detail.octaves = 4;
    Albedo albedo;
    albedo.source = std::make_shared<RasterAlbedo>(std::vector<Raster<float>>{map.raster}, options);
    const RasterGrid grid = RasterGrid::Centered({0.0, 0.0}, {160.0, 160.0}, 0.5);  // meters
    const std::vector<Raster<float>> bands = EvaluateAlbedo(albedo, grid, {});
    // --8<-- [end:upscale_an_albedo_map]
    ASSERT_EQ(bands.size(), 1u);
    EXPECT_EQ(bands[0].Width(), 320u);
    EXPECT_FALSE(std::isnan(bands[0](0, 0)));
}

}  // namespace
}  // namespace terraingen
