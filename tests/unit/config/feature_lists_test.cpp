// SPDX-License-Identifier: BSD-3-Clause

#include "configs.h"
#include "support/temp_path.h"
#include "terraingen/config/generate.h"
#include "terraingen/util/bits.h"
#include "util/text.h"

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

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
    EXPECT_EQ(ReadText(csv), "x,y,diameter,age_gyr,id,manual,replace\n"
                             "0.10000000000000001,-2.5,12.345678901234567,0.25,42,0,0\n"
                             "3,4,5,0,7,1,0\n");
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
              "x,y,diameter,age_gyr,id,manual,replace,floor_depth,rim_height,depth_ratio,"
              "freshness,complex\n"
              "0.10000000000000001,-2.5,12.345678901234567,0.25,42,0,0,2.5,0.5,0.25,0.75,0\n"
              "3,4,5,0,7,1,0,1,0.125,0.20000000000000001,1,1\n");
    const auto json = TempPath("terraingen_craters_detailed.json");
    terraingen::WriteCraterList(craters, json, ListFormat::Json, attributes);
    EXPECT_NE(ReadText(json).find("\"freshness\": 0.75"), std::string::npos);
    EXPECT_NE(ReadText(json).find("\"rim_height\": 0.125"), std::string::npos);
    EXPECT_THROW(terraingen::WriteCraterList(craters, csv, ListFormat::Csv, {attributes[0]}),
                 std::invalid_argument);
}

TEST(WriteCraterList, ReportsAFullDevice) {
#if defined(_WIN32)
    GTEST_SKIP() << "no full device on Windows";
#endif
    if (!std::filesystem::exists("/dev/full")) {
        GTEST_SKIP() << "no /dev/full";
    }
    CraterList many;
    for (std::uint64_t id = 0; id < 20000; ++id) {
        many.Append({1.0, 2.0, 3.0, 0.0, id});
    }
    EXPECT_THROW(terraingen::WriteCraterList(many, "/dev/full", ListFormat::Csv),
                 std::runtime_error);
    terraingen::BoulderList boulders;
    for (std::uint64_t id = 0; id < 20000; ++id) {
        boulders.Append(terraingen::Boulder{});
    }
    EXPECT_THROW(terraingen::WriteBoulderList(boulders, "/dev/full", ListFormat::Json),
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
    const terraingen::BoulderList boulders{known, terraingen::Boulder{}};
    const auto csv = TempPath("terraingen_boulders.csv");
    terraingen::WriteBoulderList(boulders, csv, ListFormat::Csv);
    const std::string text = ReadText(csv);
    EXPECT_EQ(
        text.rfind("x,y,ground,diameter,height,burial,yaw_deg,lean_deg,id,model,wear,parent,piece,"
                   "rest,tilt_deg,tilt_azimuth_deg\n"
                   "1.5,-2,3.25,0.5,",
                   0),
        0u)
        << text;
    EXPECT_NE(text.find("\n0,0,,"), std::string::npos) << text;
    const auto json = TempPath("terraingen_boulders.json");
    terraingen::WriteBoulderList(boulders, json, ListFormat::Json);
    EXPECT_NE(ReadText(json).find("\"ground\": null"), std::string::npos);
    EXPECT_NE(ReadText(json).find("\"ground\": 3.25"), std::string::npos);
    EXPECT_THROW(terraingen::WriteBoulderList({}, TempPath("missing/dir/r.csv"), ListFormat::Csv),
                 std::runtime_error);
}

TEST(WriteBoulderList, WritesAnglesInDegrees) {
    terraingen::Boulder boulder;
    boulder.yaw = 0.5 * terraingen::kPi;
    boulder.lean = 0.25 * terraingen::kPi;
    boulder.tilt = terraingen::kPi / 6.0;
    boulder.tiltAzimuth = terraingen::kPi;
    const auto json = TempPath("terraingen_boulder_angles.json");
    terraingen::WriteBoulderList({boulder}, json, ListFormat::Json);
    const nlohmann::json written = nlohmann::json::parse(ReadText(json))["boulders"][0];
    EXPECT_NEAR(written["yaw_deg"].get<double>(), 90.0, 1e-12);
    EXPECT_NEAR(written["lean_deg"].get<double>(), 45.0, 1e-12);
    EXPECT_NEAR(written["tilt_deg"].get<double>(), 30.0, 1e-12);
    EXPECT_NEAR(written["tilt_azimuth_deg"].get<double>(), 180.0, 1e-12);
}

TEST(WriteBoulderModels, RejectsOtherFilesAnglesAndNoBoulders) {
    terraingen::TerrainGenConfig config = terraingen::testing::Small();
    terraingen::testing::Region(config) = terraingen::testing::GridRegion{200, 200, 0.5};
    terraingen::BoulderLayerSpec boulders;
    boulders.sizes = terraingen::GolombekRappBouldersSpec{};
    boulders.boulders.sizes.coverage = 0.3;
    boulders.boulders.sizes.minDiameter = 0.5;
    boulders.boulders.rendering.modelThreshold.minDiameter = 1.5;
    boulders.boulders.rendering.shapes.subdivisions = 0;
    config.layers.push_back(boulders);
    const auto models =
        terraingen::testing::BouldersOf(terraingen::GenerateFromConfig(config)).models;
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

    terraingen::WriteBoulderModels(models, directory / "baked.glb", 0.0, {}, {}, false);
    const auto instanced = terraingen::ReadMesh(directory / "x.glb").mesh;
    const auto baked = terraingen::ReadMesh(directory / "baked.glb").mesh;
    EXPECT_EQ(baked.triangles.size(), instanced.triangles.size()) << "the same boulders";
    const auto a = terraingen::Bounds(instanced);
    const auto b = terraingen::Bounds(baked);
    EXPECT_NEAR(a.min.x, b.min.x, 1e-4);
    EXPECT_NEAR(a.max.z, b.max.z, 1e-4);
    EXPECT_GT(std::filesystem::file_size(directory / "baked.glb"),
              std::filesystem::file_size(directory / "x.glb"))
        << "each boulder written whole rather than its shape once";
}

namespace {

/// Writes `text` to a temporary file `name` and gives its path.
std::filesystem::path WriteTemp(const std::string& name, const std::string& text) {
    const auto path = TempPath(name);
    std::ofstream(path) << text;
    return path;
}

}  // namespace

TEST(ReadCraterList, ReadsBackWhatWriteCraterListWrites) {
    CraterList craters;
    craters.Append({0.1, -2.5, 12.345678901234567, 0.25, 18446744073709551557u, false});
    terraingen::Crater replacing{3.0, 4.0, 5.0, 0.0, 7u, true, 1.0, 0.25};
    replacing.Replace();
    craters.Append(replacing);
    for (const char* name : {"terraingen_read.csv", "terraingen_read.json"}) {
        const auto path = TempPath(name);
        terraingen::WriteCraterList(craters, path,
                                    terraingen::HasExtension(path, {".json"}) ? ListFormat::Json
                                                                              : ListFormat::Csv);
        const CraterList read = terraingen::ReadCraterList(path);
        ASSERT_EQ(read.Size(), 2u) << name;
        terraingen::Crater expected = replacing;
        expected.depth.reset();
        expected.rimHeight.reset();
        EXPECT_EQ(read.At(0), craters.At(0)) << name;
        EXPECT_EQ(read.At(1), expected) << name << ": depths come from attributes only";
    }
}

TEST(ReadCraterList, TakesRealizedDepthsAndRimsFromAttributes) {
    CraterList craters;
    craters.Append({1.0, 2.0, 20.0, 0.5, 3u});
    const std::vector<terraingen::CraterAttributes> attributes{{3.0, 0.5, 0.175, 0.8, false}};
    for (const char* name : {"terraingen_realized.csv", "terraingen_realized.json"}) {
        const auto path = TempPath(name);
        terraingen::WriteCraterList(craters, path,
                                    terraingen::HasExtension(path, {".json"}) ? ListFormat::Json
                                                                              : ListFormat::Csv,
                                    attributes);
        const terraingen::Crater read = terraingen::ReadCraterList(path).At(0);
        EXPECT_DOUBLE_EQ(*read.depth, 3.5) << name;
        EXPECT_EQ(read.rimHeight, 0.5) << name;
    }
}

TEST(ReadCraterList, ReadsCatalogsInTheSameForm) {
    const CraterList csv = terraingen::ReadCraterList(
        WriteTemp("terraingen_catalog.csv", " Diameter , X,y,\"depth_m\",rim_height_m,replace\r\n"
                                            "30,1.5,-2,6,,1\r\n"
                                            "\n"
                                            "10,0,0,,2,\n"));
    ASSERT_EQ(csv.Size(), 2u);
    EXPECT_EQ(csv.At(0).diameter, 30.0);
    EXPECT_EQ(csv.At(0).x, 1.5);
    EXPECT_EQ(csv.At(0).depth, 6.0);
    EXPECT_FALSE(csv.At(0).rimHeight);
    EXPECT_TRUE(csv.At(0).replace);
    EXPECT_EQ(csv.At(0).id, 0u);
    EXPECT_FALSE(csv.At(1).depth);
    EXPECT_FALSE(csv.At(1).rimHeight) << "a rim height goes with a depth only";
    EXPECT_FALSE(csv.At(1).replace);
    const CraterList json = terraingen::ReadCraterList(WriteTemp(
        "terraingen_catalog.json",
        R"([{"x": 1, "y": 2, "diameter": 3, "replace": true, "depth_m": null, "id": null}])"));
    ASSERT_EQ(json.Size(), 1u);
    EXPECT_TRUE(json.At(0).replace);
    EXPECT_FALSE(json.At(0).depth);
}

TEST(ReadCraterList, ReportsMalformedLists) {
    const auto rejects = [](const std::string& name, const std::string& text,
                            const std::string& message) {
        try {
            terraingen::ReadCraterList(WriteTemp(name, text));
            ADD_FAILURE() << "accepted " << text;
        } catch (const std::invalid_argument& error) {
            EXPECT_NE(std::string(error.what()).find(message), std::string::npos) << error.what();
        }
    };
    rejects("terraingen_bad.csv", "x,y\n1,2\n", "row 0: needs diameter");
    rejects("terraingen_bad.csv", "x,y,diameter\n1,2,3\n1,two,3\n", "row 1: y 'two' is not");
    rejects("terraingen_bad.csv", "x,y,diameter,id\n1,2,3,-4\n", "id '-4'");
    rejects("terraingen_bad.json", "{", "not valid JSON");
    rejects("terraingen_bad.json", R"({"boulders": []})", "needs a \"craters\" array");
    rejects("terraingen_bad.json", R"({"craters": {}})", "needs a \"craters\" array");
    rejects("terraingen_bad.json", R"([1])", "row 0: not an object");
    rejects("terraingen_bad.json", R"([{"x": "1", "y": 0, "diameter": 1}])", "x is not a number");
    rejects("terraingen_bad.json", R"([{"x": 1, "y": 0, "diameter": 1, "id": -1}])",
            "id is not an unsigned integer");
    EXPECT_THROW(terraingen::ReadCraterList(TempPath("terraingen_missing.csv")),
                 std::runtime_error);
}
