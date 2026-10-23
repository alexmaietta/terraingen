#include "configs.h"
#include "support/log_capture.h"
#include "support/raster_equality.h"
#include "support/temp_path.h"
#include "terraingen/config/generate.h"
#include "terraingen/raster/resample.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

using terraingen::CraterListOutput;
using terraingen::GenerateFromConfig;
using terraingen::ListFormat;
using terraingen::TerrainGenConfig;
using terraingen::WriteOutputs;
using terraingen::testing::BouldersOf;
using terraingen::testing::CratersOf;
using terraingen::testing::ExpectSameRaster;
using terraingen::testing::HeightsOf;
using terraingen::testing::Keys;
using terraingen::testing::RasterConfig;
using terraingen::testing::ReadText;
using terraingen::testing::Region;
using terraingen::testing::Small;
using terraingen::testing::SmallRegion;
using terraingen::testing::TempPath;
using terraingen::testing::WriteDem;

TEST(WriteOutputs, ResolvesRelativePathsAndCreatesFolders) {
    TerrainGenConfig config = Small();
    const auto absolute = TempPath("terraingen_absolute.csv");
    config.outputs = {CraterListOutput{{"nested/list.json", ListFormat::Json, SmallRegion()}},
                      CraterListOutput{{absolute.string(), ListFormat::Csv, SmallRegion()}}};
    const auto terrain = GenerateFromConfig(config);
    const auto directory = TempPath("terraingen_outputs");
    std::filesystem::remove_all(directory);
    const auto written = WriteOutputs(config, terrain, directory);
    ASSERT_EQ(written.size(), 2u);
    EXPECT_EQ(written[0], directory / "nested/list.json");
    EXPECT_EQ(written[1], absolute);
    EXPECT_TRUE(std::filesystem::exists(written[0]));
    const std::string csv = ReadText(absolute);
    EXPECT_EQ(std::count(csv.begin(), csv.end(), '\n'),
              static_cast<long>(CratersOf(terrain, SmallRegion()).craters.Size() + 1));
}

TEST(WriteOutputs, WritesBoulderListsAndBoulderModels) {
    TerrainGenConfig config = Small();
    const terraingen::GridRegionSpec region{200, 200, 0.5};
    terraingen::BoulderLayerSpec boulders;
    boulders.sizes = terraingen::GolombekRappBouldersSpec{};
    boulders.boulders.sizes.coverage = 0.3;
    boulders.boulders.sizes.minDiameter = 0.5;
    boulders.boulders.rendering.relief = terraingen::BoulderRelief::Shape;
    boulders.boulders.rendering.models.minDiameter = 1.5;
    boulders.boulders.rendering.shapes.subdivisions = 1;
    config.layers.push_back(boulders);
    config.outputs = {terraingen::BoulderListOutput{{"boulders.csv", {}, region}},
                      terraingen::BoulderListOutput{{"boulders.json", ListFormat::Json, region}},
                      terraingen::BoulderModelsOutput{"boulders.glb", 35.0, region},
                      terraingen::BoulderModelsOutput{"boulders.stl", 35.0, region}};
    const auto terrain = GenerateFromConfig(config);
    ASSERT_FALSE(BouldersOf(terrain, region).models.empty());
    for (const auto& boulder : BouldersOf(terrain, region).models) {
        ASSERT_TRUE(boulder.boulder.ground.has_value());
        EXPECT_GE(boulder.boulder.diameter, 1.5);
    }
    const auto directory = TempPath("terraingen_boulder_outputs");
    std::filesystem::remove_all(directory);
    const auto written = WriteOutputs(config, terrain, directory);
    ASSERT_EQ(written.size(), 4u);
    const std::string csv = ReadText(written[0]);
    EXPECT_EQ(
        csv.rfind("x,y,ground,diameter,height,burial,yaw_deg,lean_deg,id,model,wear,parent,piece,"
                  "rest,tilt_deg,tilt_azimuth_deg\n",
                  0),
        0u);
    EXPECT_EQ(std::count(csv.begin(), csv.end(), '\n'),
              static_cast<long>(BouldersOf(terrain, region).boulders.size() + 1));
    EXPECT_NE(ReadText(written[1]).find("\"model\": true"), std::string::npos);
    EXPECT_EQ(ReadText(written[2]).substr(0, 4), "glTF");
    EXPECT_EQ(ReadText(written[3]).substr(0, 10), "TerrainGen") << "a binary STL";

    const auto generations = [&](const TerrainGenConfig& outputs) {
        const terraingen::testing::LogCapture capture(terraingen::LogLevel::Debug);
        WriteOutputs(outputs, terrain, directory);
        return capture.Matching("boulders", terraingen::LogLevel::Debug).size();
    };
    TerrainGenConfig one = config;
    one.outputs.resize(1);
    EXPECT_GT(generations(one), 0u);
    EXPECT_EQ(generations(config), generations(one))
        << "the outputs over one region share the boulders placed for the first";

    terraingen::BoulderLayerSpec none;
    none.boulders.sizes.minDiameter = 0.5;
    config.layers.back() = none;
    config.outputs = {terraingen::BoulderModelsOutput{"empty.glb", 35.0, region}};
    EXPECT_TRUE(WriteOutputs(config, GenerateFromConfig(config), directory).empty())
        << "nothing to write without models";
}

TEST(WriteOutputs, WritesHeightmapsWithTheSourceProjection) {
    const auto folder = TempPath("terraingen_heightmap");
    const auto dem = WriteDem(folder, "dem.tif");
    TerrainGenConfig config = RasterConfig("dem.tif");
    config.outputs = {
        terraingen::HeightmapOutput{"out/height.tif", terraingen::TiffCompression::Deflate, 16}};
    const auto terrain = GenerateFromConfig(config, folder);
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
    TerrainGenConfig config = RasterConfig("dem.tif");
    terraingen::RasterAlbedoSpec raster{"map.tif", {0}, 0.0001, 0.0, {}};
    raster.options.detail.enabled = false;
    config.albedo = terraingen::AlbedoSpec{raster, {}};
    config.outputs = {terraingen::AlbedoOutput{"out/albedo.tif"}};
    const auto terrain = GenerateFromConfig(config, folder);
    const auto written = WriteOutputs(config, terrain, folder);
    ASSERT_EQ(written.size(), 1u);
    const auto read = terraingen::ReadGeoTiff(written[0]);
    EXPECT_EQ(read.geoKeys, Keys());
    EXPECT_EQ(read.raster.Transform(), HeightsOf(terrain).Transform());
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
    EXPECT_NEAR(terraingen::testing::AlbedoOf(GenerateFromConfig(config, folder))[0](3, 4), sampled,
                1e-3f);
    config.albedo.reset();
    EXPECT_THROW(WriteOutputs(config, GenerateFromConfig(Small()), folder), std::runtime_error);
}

TEST(WriteOutputs, WritesGeographicRastersAndListsOnASphere) {
    TerrainGenConfig config = terraingen::testing::OnASphere();
    config.albedo = terraingen::AlbedoSpec{terraingen::SyntheticAlbedoSpec{}, {}};
    const terraingen::GridRegionSpec region = Region(config);
    config.outputs = {
        terraingen::testing::Heightmap("height.tif", region),
        terraingen::AlbedoOutput{{"albedo.tif", terraingen::TiffCompression::Lzw, 256, region}},
        CraterListOutput{{"craters.csv", ListFormat::Csv, region}}};
    const auto generated = GenerateFromConfig(config, {}, 2);
    const auto directory = TempPath("terraingen_sphere");
    std::filesystem::remove_all(directory);
    const auto written = WriteOutputs(config, generated, directory);
    ASSERT_EQ(written.size(), 3u);
    const auto read = terraingen::ReadGeoTiff(written[0]);
    EXPECT_TRUE(read.geoKeys.IsGeographic());
    EXPECT_EQ(read.raster.Transform(), HeightsOf(generated).Transform());
    EXPECT_TRUE(terraingen::ReadGeoTiffInfo(written[1]).geoKeys.IsGeographic());
}

TEST(WriteOutputs, WritesBoulderModelsOnASphereAboutTheGridCentre) {
    TerrainGenConfig config;
    config.body = terraingen::SphereSurfaceSpec{1737400.0};
    const terraingen::GridRegionSpec region{160, 160, 0.25, -30.0, 60.0};
    terraingen::BoulderLayerSpec boulders;
    boulders.sizes = terraingen::GolombekRappBouldersSpec{};
    boulders.boulders.sizes.coverage = 0.3;
    boulders.boulders.sizes.minDiameter = 0.5;
    boulders.boulders.sizes.maxDiameter = 3.0;
    boulders.boulders.rendering.models.minDiameter = 1.0;
    config.layers = {boulders};
    config.outputs = {terraingen::BoulderModelsOutput{"boulders.stl", 35.0, region}};
    const auto terrain = GenerateFromConfig(config);
    ASSERT_FALSE(BouldersOf(terrain, region).models.empty());
    const auto directory = TempPath("terraingen_sphere_boulders");
    std::filesystem::remove_all(directory);
    const auto written = WriteOutputs(config, terrain, directory);
    ASSERT_EQ(written.size(), 1u);
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    for (const auto& boulder : BouldersOf(terrain, region).models) {
        const auto placed = terraingen::PlacementAtSite(
            boulder.placement, {boulder.boulder.x, boulder.boulder.y}, {-30.0, 60.0}, moon);
        EXPECT_LT(std::fabs(placed.pose.translation.x), 25.0);
        EXPECT_LT(std::fabs(placed.pose.translation.y), 25.0);
        EXPECT_LT(std::fabs(placed.pose.translation.z), 5.0);
    }
    EXPECT_GT(std::filesystem::file_size(written[0]), 1000u);
}

TEST(WriteOutputs, WritesWholeBodyMeshes) {
    TerrainGenConfig config;
    config.body = terraingen::SphereSurfaceSpec{1737400.0};
    config.outputs = {terraingen::MeshOutput{"body.glb", 6, std::nullopt},
                      terraingen::MeshOutput{"body.stl", 4, 20000.0}};
    const auto terrain = GenerateFromConfig(config);
    const auto directory = TempPath("terraingen_body_mesh");
    std::filesystem::remove_all(directory);
    const auto written = WriteOutputs(config, terrain, directory);
    ASSERT_EQ(written.size(), 2u);
    EXPECT_GT(std::filesystem::file_size(written[0]), 1000u);
    EXPECT_GT(std::filesystem::file_size(written[1]), 1000u);
}

TEST(WriteOutputs, WritesEllipsoidBodiesAsIcqShapeModels) {
    TerrainGenConfig config;
    config.body = terraingen::EllipsoidSurfaceSpec{{267500.0, 147000.0, 104500.0}};
    config.outputs = {terraingen::MeshOutput{"body.icq", 8, std::nullopt}};
    EXPECT_EQ(terraingen::testing::ValidationErrorPath(config), "valid");
    const auto terrain = GenerateFromConfig(config);
    const auto directory = TempPath("terraingen_body_icq");
    std::filesystem::remove_all(directory);
    const auto written = WriteOutputs(config, terrain, directory);
    ASSERT_EQ(written.size(), 1u);
    const terraingen::IcqModel model = terraingen::ReadIcq(written[0]);
    EXPECT_EQ(model.q, 8u);
    const terraingen::Vec3d tip = model.vertices[model.Index(4, 4, 5)];
    EXPECT_NEAR(tip.x, 267500.0, 1e-2) << "face 5 is centred on +x, in metres once read";
}

TEST(WriteOutputs, DrawsBoulderModelsFromRockFilesAndWritesTheTerrainMesh) {
    const auto directory = TempPath("terraingen_rock_outputs");
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto cube = terraingen::BaseSolidMesh(terraingen::BaseSolid::Cube);
    terraingen::WriteMesh(
        directory / "rock.obj",
        {terraingen::Scaled(cube, {1.0, 0.7, 0.5}), {}, {"basalt", {0.2, 0.2, 0.2}}});
    TerrainGenConfig config = Small();
    const terraingen::GridRegionSpec region{120, 100, 0.5};
    terraingen::BoulderLayerSpec boulders;
    boulders.sizes = terraingen::GolombekRappBouldersSpec{};
    boulders.boulders.sizes.coverage = 0.3;
    boulders.boulders.sizes.minDiameter = 0.5;
    boulders.boulders.rendering.models.minDiameter = 1.5;
    boulders.shapeFiles = {"rock.obj"};
    config.layers.push_back(boulders);
    config.outputs = {terraingen::BoulderModelsOutput{"boulders.obj", 35.0, region},
                      terraingen::TerrainMeshOutput{"terrain.glb", region}};
    const auto terrain = GenerateFromConfig(config, directory);
    const auto models = BouldersOf(terrain).models;
    ASSERT_FALSE(models.empty());
    const auto written = WriteOutputs(config, terrain, directory);
    ASSERT_EQ(written.size(), 2u);
    const auto rocks = terraingen::ReadMesh(written[0]);
    EXPECT_EQ(rocks.material.name, "basalt");
    EXPECT_EQ(rocks.mesh.triangles.size(), 12u * models.size()) << "a cube per boulder model";
    const auto ground = terraingen::ReadMesh(written[1]);
    EXPECT_EQ(ground.mesh.positions.size(), 120u * 100u);
    EXPECT_EQ(ground.mesh.triangles.size(), 2u * 119u * 99u);

    config.layers.back() = [&] {
        terraingen::BoulderLayerSpec missing = boulders;
        missing.shapeFiles = {"missing.obj"};
        return missing;
    }();
    try {
        terraingen::ValidateConfig(config, directory);
        FAIL() << "a missing rock file is an error";
    } catch (const terraingen::ConfigError& error) {
        EXPECT_EQ(error.Path(),
                  "layers[" + std::to_string(config.layers.size() - 1) + "].shape_files[0]");
    }
    config.layers.pop_back();
    config.outputs = {terraingen::TerrainMeshOutput{"terrain.fbx"}};
    EXPECT_THROW(terraingen::ValidateConfig(config, directory), terraingen::ConfigError);
}

TEST(WriteOutputs, WritesWholeBodyListsAndRejectsGridsItDidNotGenerate) {
    TerrainGenConfig config;
    config.body = terraingen::SphereSurfaceSpec{50000.0};
    terraingen::CraterLayerSpec craters;
    craters.minDiameter = 2000.0;
    craters.maxDiameter = 20000.0;
    craters.density = terraingen::testing::LunarDensitySpec();
    config.layers = {craters};
    config.outputs = {CraterListOutput{{"craters.csv", ListFormat::Csv, {}, 500.0}}};
    const auto generated = GenerateFromConfig(config);
    const auto directory = TempPath("terraingen_whole_body_list");
    std::filesystem::remove_all(directory);
    const auto written = WriteOutputs(config, generated, directory);
    ASSERT_EQ(written.size(), 1u);
    const std::string csv = ReadText(written[0]);
    EXPECT_GT(std::count(csv.begin(), csv.end(), '\n'), 10);

    TerrainGenConfig other = Small();
    EXPECT_THROW(WriteOutputs(other, generated, directory), std::runtime_error)
        << "the heightmap's grid was not generated";
    other.outputs = {terraingen::MeshOutput{"m.glb"}};
    other.body = config.body;
    EXPECT_NO_THROW(WriteOutputs(other, generated, directory));
}

TEST(WriteOutputs, WritesMaterialsAndTheirPhotometry) {
    TerrainGenConfig config = Small();
    config.layers.push_back(terraingen::SandFillLayerSpec{{6.0, 0.8, 0.5, 0.02}});
    terraingen::MaterialsSpec materials;
    materials.photometry.reference = terraingen::PhotometryReference::MarsPancam;
    materials.photometry.classes[3] = terraingen::HapkeParameters{0.7, 0.4, 0.3, 15.0};
    config.materials = materials;
    config.outputs.push_back(terraingen::MaterialsOutput{
        {"materials.tif", terraingen::TiffCompression::Lzw, 256, SmallRegion()}});
    config.outputs.push_back(terraingen::PhotometryOutput{
        {"hapke.tif", terraingen::TiffCompression::Lzw, 256, SmallRegion()}});
    const auto generated = GenerateFromConfig(config);
    ASSERT_EQ(generated.grids.size(), 1u) << "one grid for heights, materials and photometry";
    ASSERT_EQ(generated.grids[0].materials.size(), terraingen::kMaterialClasses);
    const auto directory = TempPath("terraingen_materials");
    std::filesystem::remove_all(directory);
    const auto written = WriteOutputs(config, generated, directory);
    ASSERT_EQ(written.size(), 3u);
    EXPECT_EQ(terraingen::ReadGeoTiffInfo(written[1]).bands, terraingen::kMaterialClasses);
    EXPECT_EQ(terraingen::ReadGeoTiffInfo(written[2]).bands, 4u);
    const auto albedo = terraingen::ReadGeoTiff(written[2]).raster;
    for (const float w : albedo.Values()) {
        ASSERT_GE(w, 0.52f - 1e-6f);
        ASSERT_LE(w, 0.76f + 1e-6f);
    }
    const auto& sand =
        generated.grids[0].materials[static_cast<std::size_t>(terraingen::MaterialClass::Sand)];
    EXPECT_GT(*std::max_element(sand.Values().begin(), sand.Values().end()), 0.5f);

    TerrainGenConfig without = Small();
    without.outputs.push_back(terraingen::MaterialsOutput{
        {"m.tif", terraingen::TiffCompression::Lzw, 256, SmallRegion()}});
    EXPECT_THROW(WriteOutputs(without, GenerateFromConfig(Small()), directory), std::runtime_error)
        << "no materials generated";
}
