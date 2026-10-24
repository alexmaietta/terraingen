// SPDX-License-Identifier: BSD-3-Clause

#include "configs.h"
#include "support/log_capture.h"
#include "support/raster_equality.h"
#include "support/temp_path.h"
#include "terraingen/config/generate.h"
#include "terraingen/raster/resample.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <variant>

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
    const terraingen::testing::GridRegion region{200, 200, 0.5};
    terraingen::BoulderLayerSpec boulders;
    boulders.sizes = terraingen::GolombekRappBouldersSpec{};
    boulders.boulders.sizes.coverage = 0.3;
    boulders.boulders.sizes.minDiameter = 0.5;
    boulders.boulders.rendering.relief = terraingen::BoulderRelief::Shape;
    boulders.boulders.rendering.modelThreshold.minDiameter = 1.5;
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
              static_cast<long>(BouldersOf(terrain, region).boulders.Size() + 1));
    EXPECT_NE(ReadText(written[1]).find("\"model\": true"), std::string::npos);
    EXPECT_EQ(ReadText(written[2]).substr(0, 4), "glTF");
    EXPECT_EQ(ReadText(written[3]).substr(0, 10), "TerrainGen") << "a binary STL";

    ASSERT_EQ(terrain.grids.size(), 1u);
    ASSERT_TRUE(terrain.Grid().boulders);
    EXPECT_EQ(terrain.Grid().boulders->boulders.Size(),
              BouldersOf(terrain, region).boulders.Size());
    EXPECT_TRUE(terrain.Grid().heights.Empty()) << "lists and models evaluate no heights";

    const auto placements = [&](const TerrainGenConfig& outputs) {
        const terraingen::testing::LogCapture capture(terraingen::LogLevel::Debug);
        GenerateFromConfig(outputs);
        return capture.Matching("boulders", terraingen::LogLevel::Debug).size();
    };
    TerrainGenConfig one = config;
    one.outputs.resize(1);
    EXPECT_GT(placements(one), 0u);
    EXPECT_EQ(placements(config), placements(one))
        << "the outputs over one grid share the boulders placed for the first";
    const terraingen::testing::LogCapture writing(terraingen::LogLevel::Debug);
    WriteOutputs(config, terrain, directory);
    EXPECT_TRUE(writing.Matching("boulders", terraingen::LogLevel::Debug).empty())
        << "writing places no boulders";

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
    const auto center = read.raster.CellCenter(3, 4);
    const float sampled =
        terraingen::SampleRaster(map, center.x, center.y, terraingen::ResampleMethod::Cubic);
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
    const terraingen::OutputAreaSpec region = Region(config);
    config.outputs = {
        terraingen::HeightmapOutput{"height.tif", terraingen::TiffCompression::Lzw, 256, region},
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

TEST(WriteOutputs, WritesBoulderModelsOnASphereAboutTheGridCenter) {
    TerrainGenConfig config;
    config.body = terraingen::SphereSurfaceSpec{1737400.0};
    const terraingen::testing::GridRegion region{160, 160, 0.25, -30.0, 60.0};
    terraingen::BoulderLayerSpec boulders;
    boulders.sizes = terraingen::GolombekRappBouldersSpec{};
    boulders.boulders.sizes.coverage = 0.3;
    boulders.boulders.sizes.minDiameter = 0.5;
    boulders.boulders.sizes.maxDiameter = 3.0;
    boulders.boulders.rendering.modelThreshold.minDiameter = 1.0;
    config.layers = {boulders};
    config.outputs = {terraingen::BoulderModelsOutput{
        "boulders.stl", 35.0, region.Area(terraingen::Surface::Sphere(1737400.0))}};
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
    config.outputs = {terraingen::MeshOutput{"body.glb", 4, 20000.0},
                      terraingen::MeshOutput{"body.stl", 4, 20000.0}};
    const auto terrain = GenerateFromConfig(config);
    ASSERT_TRUE(terrain.bodyMesh);
    EXPECT_FALSE(terrain.bodyIcq);
    EXPECT_EQ(terrain.bodyMesh->triangles.size(),
              terraingen::BodyMesh(terrain.generator, 4, 20000.0).triangles.size());
    EXPECT_TRUE(terrain.grids.empty());
    const auto directory = TempPath("terraingen_body_mesh");
    std::filesystem::remove_all(directory);
    const auto written = WriteOutputs(config, terrain, directory);
    ASSERT_EQ(written.size(), 2u);
    EXPECT_GT(std::filesystem::file_size(written[0]), 1000u);
    EXPECT_GT(std::filesystem::file_size(written[1]), 1000u);
    const auto read = terraingen::ReadMesh(written[1]);
    EXPECT_EQ(read.mesh.triangles.size(), terrain.bodyMesh->triangles.size());
}

TEST(WriteOutputs, WritesEllipsoidBodiesAsIcqShapeModels) {
    TerrainGenConfig config;
    config.body = terraingen::EllipsoidSurfaceSpec{{267500.0, 147000.0, 104500.0}};
    config.outputs = {terraingen::MeshOutput{"body.icq", 8, std::nullopt}};
    EXPECT_EQ(terraingen::testing::ValidationErrorPath(config), "valid");
    const auto terrain = GenerateFromConfig(config);
    ASSERT_TRUE(terrain.bodyIcq);
    EXPECT_FALSE(terrain.bodyMesh);
    const auto directory = TempPath("terraingen_body_icq");
    std::filesystem::remove_all(directory);
    const auto written = WriteOutputs(config, terrain, directory);
    ASSERT_EQ(written.size(), 1u);
    const terraingen::IcqModel model = terraingen::ReadIcq(written[0]);
    EXPECT_EQ(model.q, 8u);
    const terraingen::Vec3d tip = model.vertices[model.Index(4, 4, 5)];
    EXPECT_NEAR(tip.x, 267500.0, 1e-2) << "face 5 is centered on +x, in meters once read";
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
    const terraingen::testing::GridRegion region{120, 100, 0.5};
    terraingen::BoulderLayerSpec boulders;
    boulders.sizes = terraingen::GolombekRappBouldersSpec{};
    boulders.boulders.sizes.coverage = 0.3;
    boulders.boulders.sizes.minDiameter = 0.5;
    boulders.boulders.rendering.modelThreshold.minDiameter = 1.5;
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
    craters.ages = terraingen::testing::LunarAgesSpec();
    config.layers = {craters};
    terraingen::OutputAreaSpec whole;
    whole.pixelSize = 500.0;
    config.outputs = {CraterListOutput{{"craters.csv", ListFormat::Csv, whole}}};
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
    EXPECT_THROW(WriteOutputs(other, generated, directory), std::runtime_error)
        << "no body mesh was generated";
    other.outputs = {terraingen::MeshOutput{""}};
    EXPECT_TRUE(WriteOutputs(other, generated, directory).empty()) << "nothing to write";
}

TEST(WriteOutputs, WritesExactlyWhatGenerateFromConfigReturned) {
    TerrainGenConfig config = terraingen::testing::WithAlbedo();
    config.outputs.push_back(CraterListOutput{{"craters.json", ListFormat::Json, SmallRegion()}});
    config.outputs.push_back(terraingen::TerrainMeshOutput{"terrain.obj", SmallRegion()});
    terraingen::GeneratedOutputs generated = GenerateFromConfig(config);
    ASSERT_EQ(generated.grids.size(), 1u) << "every output covers one grid";
    terraingen::GeneratedGrid& grid = generated.grids[0];
    for (float& height : grid.heights.Values()) {
        height = 7.0f;
    }
    grid.albedo[0].Values()[0] = 0.25f;
    grid.craters->craters = terraingen::CraterList{};
    grid.craters->attributes.clear();
    grid.mesh->triangles.resize(1);
    const auto directory = TempPath("terraingen_exactly_returned");
    std::filesystem::remove_all(directory);
    const auto written = WriteOutputs(config, generated, directory);
    ASSERT_EQ(written.size(), 4u);
    ExpectSameRaster(terraingen::ReadGeoTiff(written[0]).raster, grid.heights);
    EXPECT_EQ(terraingen::ReadGeoTiff(written[1]).raster.Values()[0], 0.25f);
    EXPECT_EQ(nlohmann::json::parse(ReadText(written[2]))["craters"].size(), 0u);
    EXPECT_EQ(terraingen::ReadMesh(written[3]).mesh.triangles.size(), 1u);
}

TEST(WriteOutputs, ReturnsOutputsWithoutPathsAndWritesNothingForThem) {
    TerrainGenConfig config = terraingen::testing::WithAlbedo();
    for (terraingen::OutputSpec& output : config.outputs) {
        std::visit([](auto& o) { o.path.clear(); }, output);
    }
    config.outputs.push_back(CraterListOutput{{"", ListFormat::Csv, SmallRegion()}});
    config.outputs.push_back(terraingen::TerrainMeshOutput{"", SmallRegion()});
    config.outputs.push_back(terraingen::ReportOutput{});
    EXPECT_EQ(terraingen::testing::ValidationErrorPath(config), "valid");
    const auto generated = GenerateFromConfig(config);
    ASSERT_EQ(generated.grids.size(), 1u);
    const terraingen::GeneratedGrid& grid = generated.Grid();
    EXPECT_EQ(grid.grid, SmallRegion().Grid());
    EXPECT_EQ(grid.heights.Width(), 65u);
    EXPECT_EQ(grid.albedo.size(), 3u);
    ASSERT_TRUE(grid.craters);
    EXPECT_EQ(grid.craters->craters.Ids(), CratersOf(generated, SmallRegion()).craters.Ids());
    ASSERT_TRUE(grid.mesh);
    EXPECT_EQ(grid.mesh->positions.size(), 65u * 49u);
    EXPECT_FALSE(grid.boulders) << "no output asked for boulders";
    EXPECT_TRUE(grid.materials.empty());
    ASSERT_TRUE(generated.report) << "a report without a path is returned";
    EXPECT_EQ(generated.report->grids.size(), 1u);
    const auto directory = TempPath("terraingen_in_memory");
    std::filesystem::remove_all(directory);
    EXPECT_TRUE(WriteOutputs(config, generated, directory).empty());
    EXPECT_FALSE(std::filesystem::exists(directory)) << "not even the folder";
}

TEST(WriteOutputs, WritesTheReportItReturned) {
    TerrainGenConfig config = Small();
    config.outputs.push_back(CraterListOutput{{"", ListFormat::Csv, SmallRegion()}});
    config.outputs.push_back(terraingen::ReportOutput{"out/report.json"});
    const auto generated = GenerateFromConfig(config);
    ASSERT_TRUE(generated.report);
    const auto directory = TempPath("terraingen_report_output");
    std::filesystem::remove_all(directory);
    const auto written = WriteOutputs(config, generated, directory);
    ASSERT_EQ(written.size(), 2u);
    EXPECT_EQ(written[1], directory / "out" / "report.json");
    const nlohmann::json json = nlohmann::json::parse(ReadText(written[1]));
    EXPECT_EQ(json, nlohmann::json::parse(terraingen::ReportToJson(*generated.report)));
    std::size_t counted = 0;
    for (const nlohmann::json& bin : json.at("grids").at(0).at("crater_bins")) {
        counted += bin.at("count").get<std::size_t>();
    }
    EXPECT_EQ(counted, generated.Grid().craters->craters.Size());
    EXPECT_EQ(json.at("layers").size(), 2u);

    TerrainGenConfig without = config;
    without.outputs.pop_back();
    EXPECT_THROW(WriteOutputs(config, GenerateFromConfig(without), directory), std::runtime_error)
        << "nothing to write without the report";
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
    ASSERT_EQ(generated.grids[0].photometry.size(), 4u);
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
