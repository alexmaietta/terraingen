/// @file
/// @brief Tests of `WriteOutputs` (pipeline/run.cpp).
#include "pipeline_configs.h"
#include "support/raster_equality.h"
#include "support/temp_path.h"
#include "terraingen/pipeline/run.h"
#include "terraingen/raster/resample.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

using terraingen::CraterListOutput;
using terraingen::GenerateTerrain;
using terraingen::ListFormat;
using terraingen::TerrainConfig;
using terraingen::WriteOutputs;
using terraingen::testing::ExpectSameRaster;
using terraingen::testing::Flat;
using terraingen::testing::Keys;
using terraingen::testing::RasterConfig;
using terraingen::testing::ReadText;
using terraingen::testing::Small;
using terraingen::testing::TempPath;
using terraingen::testing::WriteDem;

TEST(WriteOutputs, ResolvesRelativePathsAndCreatesFolders) {
    TerrainConfig config = Small();
    const auto absolute = TempPath("terraingen_absolute.csv");
    config.outputs = {CraterListOutput{"nested/list.json", ListFormat::Json},
                      CraterListOutput{absolute.string(), ListFormat::Csv}};
    const auto terrain = GenerateTerrain(config);
    const auto directory = TempPath("terraingen_outputs");
    std::filesystem::remove_all(directory);
    const auto written = WriteOutputs(config, terrain, directory);
    ASSERT_EQ(written.size(), 2u);
    EXPECT_EQ(written[0], directory / "nested/list.json");
    EXPECT_EQ(written[1], absolute);
    EXPECT_TRUE(std::filesystem::exists(written[0]));
    const std::string csv = ReadText(absolute);
    EXPECT_EQ(std::count(csv.begin(), csv.end(), '\n'),
              static_cast<long>(terrain.features.craters.Size() + 1));
}

TEST(WriteOutputs, WritesBoulderListsAndBoulderModels) {
    TerrainConfig config = Small();
    Flat(config) = {200, 200, 0.5};
    terraingen::BoulderLayerSpec boulders;
    boulders.boulders.coverage = 0.3;
    boulders.boulders.minDiameter = 0.5;
    boulders.boulders.relief = terraingen::BoulderRelief::Shape;
    boulders.boulders.models.minDiameter = 1.5;
    boulders.boulders.shapes.subdivisions = 1;
    config.layers.push_back(boulders);
    config.outputs = {terraingen::BoulderListOutput{"boulders.csv"},
                      terraingen::BoulderListOutput{"boulders.json", ListFormat::Json},
                      terraingen::BoulderModelsOutput{"boulders.glb"},
                      terraingen::BoulderModelsOutput{"boulders.ply"}};
    const auto terrain = GenerateTerrain(config);
    ASSERT_FALSE(terrain.features.boulderModels.empty());
    for (const auto& boulder : terrain.features.boulderModels) {
        ASSERT_TRUE(boulder.boulder.ground.has_value());
        EXPECT_GE(boulder.boulder.diameter, 1.5);
    }
    const auto directory = TempPath("terraingen_boulder_outputs");
    std::filesystem::remove_all(directory);
    const auto written = WriteOutputs(config, terrain, directory);
    ASSERT_EQ(written.size(), 4u);
    const std::string csv = ReadText(written[0]);
    EXPECT_EQ(csv.rfind("x,y,ground,diameter,height,burial,yaw,lean,id,model\n", 0), 0u);
    EXPECT_EQ(std::count(csv.begin(), csv.end(), '\n'),
              static_cast<long>(terrain.features.boulders.size() + 1));
    EXPECT_NE(ReadText(written[1]).find("\"model\": true"), std::string::npos);
    EXPECT_EQ(ReadText(written[2]).substr(0, 4), "glTF");
    EXPECT_EQ(ReadText(written[3]).substr(0, 3), "ply");

    terraingen::BoulderLayerSpec none;
    none.boulders.minDiameter = 0.5;
    config.layers.back() = none;
    config.outputs = {terraingen::BoulderModelsOutput{"empty.glb"}};
    EXPECT_TRUE(WriteOutputs(config, GenerateTerrain(config), directory).empty())
        << "nothing to write without models";
}

TEST(WriteOutputs, WritesHeightmapsWithTheSourceProjection) {
    const auto folder = TempPath("terraingen_heightmap");
    const auto dem = WriteDem(folder, "dem.tif");
    TerrainConfig config = RasterConfig("dem.tif");
    config.outputs = {
        terraingen::HeightmapOutput{"out/height.tif", terraingen::TiffCompression::Deflate, 16}};
    const auto terrain = GenerateTerrain(config, folder);
    const auto written = WriteOutputs(config, terrain, folder);
    ASSERT_EQ(written.size(), 1u);
    const auto info = terraingen::ReadGeoTiffInfo(written[0]);
    EXPECT_EQ(info.compression, terraingen::TiffCompression::Deflate);
    const auto read = terraingen::ReadGeoTiff(written[0]);
    ExpectSameRaster(read.raster, dem);
    EXPECT_EQ(read.geoKeys, Keys());
    EXPECT_EQ(read.raster.Transform(), dem.Transform());
}

TEST(WriteOutputs, WritesARasterAlbedoUpscaledWithTheSourceProjection) {
    const auto folder = TempPath("terraingen_albedo");
    WriteDem(folder, "dem.tif");
    terraingen::Raster<float> map(10, 8);
    map.SetTransform(terraingen::GeoTransform::NorthUp(1000.0, 2000.0, 20.0));
    for (std::size_t i = 0; i < map.Size(); ++i) {
        map.Values()[i] = static_cast<float>(1000 + 37 * (i % 11));
    }
    terraingen::WriteGeoTiff(folder / "map.tif", map, Keys());
    TerrainConfig config = RasterConfig("dem.tif");
    terraingen::RasterAlbedoSpec raster{"map.tif", {0}, 0.0001, 0.0, {}};
    raster.options.detail.enabled = false;
    config.albedo = terraingen::AlbedoSpec{raster, {}};
    config.outputs = {terraingen::AlbedoOutput{"out/albedo.tif"}};
    const auto terrain = GenerateTerrain(config, folder);
    const auto written = WriteOutputs(config, terrain, folder);
    ASSERT_EQ(written.size(), 1u);
    const auto read = terraingen::ReadGeoTiff(written[0]);
    EXPECT_EQ(read.geoKeys, Keys());
    EXPECT_EQ(read.raster.Transform(), terrain.heights.Transform());
    const auto centre = read.raster.CellCenter(3, 4);
    const float sampled =
        terraingen::SampleRaster(map, centre.x, centre.y, terraingen::ResampleMethod::Cubic);
    EXPECT_NEAR(read.raster(3, 4), 0.0001f * sampled, 1e-6f);
    float lowest = 1.0f;
    float highest = 0.0f;
    for (const float value : read.raster.Values()) {
        lowest = std::min(lowest, value);
        highest = std::max(highest, value);
    }
    EXPECT_GT(lowest, 0.09f);
    EXPECT_LT(highest, 0.15f);

    config.albedo->source = terraingen::RasterAlbedoSpec{"map.tif", {0}, {}, {}, {}};
    EXPECT_NEAR(GenerateTerrain(config, folder).albedo[0](3, 4), sampled, 1e-3f);
    config.albedo.reset();
    EXPECT_THROW(WriteOutputs(config, GenerateTerrain(Small()), folder), std::runtime_error);
}

TEST(WriteOutputs, WritesGeographicRastersAndListsOnASphere) {
    TerrainConfig config = terraingen::testing::OnASphere();
    config.albedo = terraingen::AlbedoSpec{terraingen::SyntheticAlbedoSpec{}, {}};
    config.outputs = {terraingen::HeightmapOutput{"height.tif"},
                      terraingen::AlbedoOutput{"albedo.tif"},
                      CraterListOutput{"craters.csv", ListFormat::Csv}};
    const auto generated = GenerateTerrain(config, {}, 2);
    const auto directory = TempPath("terraingen_sphere");
    std::filesystem::remove_all(directory);
    const auto written = WriteOutputs(config, generated, directory);
    ASSERT_EQ(written.size(), 3u);
    const auto read = terraingen::ReadGeoTiff(written[0]);
    EXPECT_TRUE(read.geoKeys.IsGeographic());
    EXPECT_EQ(read.raster.Transform(), generated.heights.Transform());
    EXPECT_TRUE(terraingen::ReadGeoTiffInfo(written[1]).geoKeys.IsGeographic());
}

TEST(WriteOutputs, WritesBoulderModelsOnASphereAboutTheGridCentre) {
    TerrainConfig config;
    config.surface = terraingen::SphereSurfaceSpec{0.0, terraingen::Body::Moon};
    terraingen::FlatSourceSpec flat{160, 160, 0.25};
    flat.centreX = -30.0;
    flat.centreY = 60.0;
    config.source = flat;
    terraingen::BoulderLayerSpec boulders;
    boulders.boulders.coverage = 0.3;
    boulders.boulders.minDiameter = 0.5;
    boulders.boulders.maxDiameter = 3.0;
    boulders.boulders.models.minDiameter = 1.0;
    config.layers = {boulders};
    config.outputs = {terraingen::BoulderModelsOutput{"boulders.ply"}};
    const auto terrain = GenerateTerrain(config);
    ASSERT_FALSE(terrain.features.boulderModels.empty());
    const auto directory = TempPath("terraingen_sphere_boulders");
    std::filesystem::remove_all(directory);
    const auto written = WriteOutputs(config, terrain, directory);
    ASSERT_EQ(written.size(), 1u);
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    for (const auto& boulder : terrain.features.boulderModels) {
        const auto placed = terraingen::PlacementAtSite(
            boulder.placement, {boulder.boulder.x, boulder.boulder.y}, {-30.0, 60.0}, moon);
        EXPECT_LT(std::fabs(placed.pose.translation.x), 25.0);
        EXPECT_LT(std::fabs(placed.pose.translation.y), 25.0);
        EXPECT_LT(std::fabs(placed.pose.translation.z), 5.0);
    }
    EXPECT_GT(std::filesystem::file_size(written[0]), 1000u);
}

TEST(WriteOutputs, WritesWholeBodyMeshes) {
    TerrainConfig config;
    config.surface = terraingen::SphereSurfaceSpec{0.0, terraingen::Body::Moon};
    config.source = terraingen::FlatSourceSpec{4, 4, 1000.0, 5.0f};
    config.outputs = {terraingen::BodyMeshOutput{"body.glb", 6, std::nullopt},
                      terraingen::BodyMeshOutput{"body.ply", 4, 20000.0}};
    const auto terrain = GenerateTerrain(config);
    const auto directory = TempPath("terraingen_body_mesh");
    std::filesystem::remove_all(directory);
    const auto written = WriteOutputs(config, terrain, directory);
    ASSERT_EQ(written.size(), 2u);
    EXPECT_GT(std::filesystem::file_size(written[0]), 1000u);
    EXPECT_GT(std::filesystem::file_size(written[1]), 1000u);
}
