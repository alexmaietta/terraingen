#include "configs.h"
#include "support/temp_path.h"
#include "terraingen/config/generate.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

using terraingen::CraterList;
using terraingen::ListFormat;
using terraingen::testing::ReadText;
using terraingen::testing::TempPath;

TEST(WriteCraterList, WritesCsvAndJsonExactly) {
    CraterList craters;
    craters.Append({0.1, -2.5, 12.345678901234567, 0.25, 42u, false});
    craters.Append({3.0, 4.0, 5.0, 0.0, 7u, true});
    const auto csv = TempPath("terraingen_craters.csv");
    terraingen::WriteCraterList(craters, csv, ListFormat::Csv);
    EXPECT_EQ(ReadText(csv), "x,y,diameter,age_gyr,id,manual\n"
                             "0.10000000000000001,-2.5,12.345678901234567,0.25,42,0\n"
                             "3,4,5,0,7,1\n");
    const auto json = TempPath("terraingen_craters.json");
    terraingen::WriteCraterList(craters, json, ListFormat::Json);
    const std::string text = ReadText(json);
    EXPECT_EQ(text.rfind("{\n  \"craters\": [\n    {\n      \"x\": 0.1,", 0), 0u) << text;
    EXPECT_NE(text.find("\"diameter\": 12.345678901234567"), std::string::npos) << text;
    EXPECT_NE(text.find("\"manual\": true"), std::string::npos);
    EXPECT_NE(text.find("\"id\": 42"), std::string::npos);
    EXPECT_EQ(text.find("freshness"), std::string::npos);
    EXPECT_THROW(
        terraingen::WriteCraterList(craters, TempPath("missing/dir/x.csv"), ListFormat::Csv),
        std::runtime_error);
}

TEST(WriteCraterList, AddsAttributesWhenGivenOnePerCrater) {
    CraterList craters;
    craters.Append({0.1, -2.5, 12.345678901234567, 0.25, 42u, false});
    craters.Append({3.0, 4.0, 5.0, 0.0, 7u, true});
    const std::vector<terraingen::CraterAttributes> attributes{{2.5, 0.5, 0.25, 0.75, false},
                                                               {1.0, 0.125, 0.2, 1.0, true}};
    const auto csv = TempPath("terraingen_craters_detailed.csv");
    terraingen::WriteCraterList(craters, csv, ListFormat::Csv, attributes);
    EXPECT_EQ(ReadText(csv),
              "x,y,diameter,age_gyr,id,manual,floor_depth,rim_height,depth_ratio,"
              "freshness,complex\n"
              "0.10000000000000001,-2.5,12.345678901234567,0.25,42,0,2.5,0.5,0.25,0.75,0\n"
              "3,4,5,0,7,1,1,0.125,0.20000000000000001,1,1\n");
    const auto json = TempPath("terraingen_craters_detailed.json");
    terraingen::WriteCraterList(craters, json, ListFormat::Json, attributes);
    EXPECT_NE(ReadText(json).find("\"freshness\": 0.75"), std::string::npos);
    EXPECT_NE(ReadText(json).find("\"rim_height\": 0.125"), std::string::npos);
    EXPECT_THROW(terraingen::WriteCraterList(craters, csv, ListFormat::Csv, {attributes[0]}),
                 std::invalid_argument);
}

TEST(WriteCraterList, ReportsAFullDevice) {
    if (!std::filesystem::exists("/dev/full")) {
        GTEST_SKIP() << "no /dev/full";
    }
    CraterList many;
    for (std::uint64_t id = 0; id < 20000; ++id) {
        many.Append({1.0, 2.0, 3.0, 0.0, id});
    }
    EXPECT_THROW(terraingen::WriteCraterList(many, "/dev/full", ListFormat::Csv),
                 std::runtime_error);
    EXPECT_THROW(terraingen::WriteBoulderList(std::vector<terraingen::Boulder>(20000), "/dev/full",
                                              ListFormat::Json),
                 std::runtime_error);
}

TEST(WriteBoulderList, WritesCsvAndJsonWithUnknownGroundsLeftOut) {
    terraingen::Boulder known;
    known.x = 1.5;
    known.y = -2.0;
    known.ground = 3.25;
    known.diameter = 0.5;
    known.id = 9;
    known.model = true;
    const std::vector<terraingen::Boulder> boulders{known, terraingen::Boulder{}};
    const auto csv = TempPath("terraingen_boulders.csv");
    terraingen::WriteBoulderList(boulders, csv, ListFormat::Csv);
    const std::string text = ReadText(csv);
    EXPECT_EQ(
        text.rfind("x,y,ground,diameter,height,burial,yaw,lean,id,model\n1.5,-2,3.25,0.5,", 0), 0u)
        << text;
    EXPECT_NE(text.find("\n0,0,,"), std::string::npos) << text;
    const auto json = TempPath("terraingen_boulders.json");
    terraingen::WriteBoulderList(boulders, json, ListFormat::Json);
    EXPECT_NE(ReadText(json).find("\"ground\": null"), std::string::npos);
    EXPECT_NE(ReadText(json).find("\"ground\": 3.25"), std::string::npos);
    EXPECT_THROW(terraingen::WriteBoulderList({}, TempPath("missing/dir/r.csv"), ListFormat::Csv),
                 std::runtime_error);
}

TEST(WriteBoulderModels, RejectsOtherFilesAnglesAndNoBoulders) {
    terraingen::TerrainConfig config = terraingen::testing::Small();
    terraingen::testing::Region(config) = {200, 200, 0.5};
    terraingen::BoulderLayerSpec boulders;
    boulders.boulders.sizes.coverage = 0.3;
    boulders.boulders.sizes.minDiameter = 0.5;
    boulders.boulders.rendering.models.minDiameter = 1.5;
    boulders.boulders.rendering.shapes.subdivisions = 0;
    config.layers.push_back(boulders);
    const auto models = terraingen::testing::BouldersOf(terraingen::GenerateTerrain(config)).models;
    ASSERT_FALSE(models.empty());
    const auto directory = TempPath("terraingen_boulder_models");
    std::filesystem::create_directories(directory);
    EXPECT_THROW(terraingen::WriteBoulderModels({}, directory / "x.glb"), std::invalid_argument);
    EXPECT_THROW(terraingen::WriteBoulderModels(models, directory / "x.fbx"),
                 std::invalid_argument);
    EXPECT_THROW(terraingen::WriteBoulderModels(models, directory / "x.glb", -1.0),
                 std::invalid_argument);
    EXPECT_THROW(terraingen::WriteBoulderModels(models, directory / "x.glb", 181.0),
                 std::invalid_argument);
    terraingen::WriteBoulderModels(models, directory / "x.glb", 0.0);
    EXPECT_EQ(ReadText(directory / "x.glb").substr(0, 4), "glTF");
}
