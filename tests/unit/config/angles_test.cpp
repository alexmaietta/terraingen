// SPDX-License-Identifier: BSD-3-Clause

#include "configs.h"
#include "terraingen/config/fields/fields.h"
#include "terraingen/config/fields/json_io.h"
#include "terraingen/config/fields/ranges.h"
#include "terraingen/util/geometry.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace config = terraingen::config;
using config::DegreesFromRadians;
using config::Json;
using config::RadiansFromDegrees;
using terraingen::ConfigError;
using terraingen::ConfigToJson;
using terraingen::DuneLayerSpec;
using terraingen::kDegree;
using terraingen::ParseConfig;
using terraingen::StrataLayerSpec;
using terraingen::TerrainGenConfig;

namespace {

/// A configuration of one layer given as JSON.
std::string OneLayer(const std::string& layer) {
    return R"({"schema_version": 1, "layers": [)" + layer + "]}";
}

/// The message reading `json` throws, or "valid".
std::string ReadError(const std::string& json) {
    try {
        terraingen::config::CheckConfigRanges(ParseConfig(json));
    } catch (const ConfigError& error) {
        return error.what();
    }
    return "valid";
}

}  // namespace

TEST(DegreesFromRadians, GivesTheShortestDegreesThatTurnBackIntoTheRadians) {
    EXPECT_EQ(DegreesFromRadians(29.0 * kDegree), 29.0);
    EXPECT_EQ(DegreesFromRadians(0.0), 0.0);
    EXPECT_EQ(DegreesFromRadians(-90.0 * kDegree), -90.0);
    for (const double radians : {0.5, 0.35, 0.03, 0.8, 4.3, 0.26, 1e-9, 123.456}) {
        EXPECT_EQ(RadiansFromDegrees(DegreesFromRadians(radians)), radians) << radians;
    }
    EXPECT_TRUE(std::isnan(DegreesFromRadians(std::nan(""))));
    EXPECT_TRUE(std::isinf(DegreesFromRadians(HUGE_VAL)));
}

TEST(ConfigAngles, ReadDegreesIntoRadians) {
    const TerrainGenConfig config = ParseConfig(OneLayer(
        R"({"type": "dunes", "wind_azimuth_deg": 90, "slip_face_angle_deg": 30, "stoss_slope_deg": 10})"));
    const auto& dunes = std::get<DuneLayerSpec>(config.layers.at(0)).dunes;
    EXPECT_EQ(dunes.windAzimuth, 90.0 * kDegree);
    EXPECT_EQ(dunes.slipFaceAngle, 30.0 * kDegree);
    EXPECT_EQ(dunes.stossSlope, 10.0 * kDegree);
}

TEST(ConfigAngles, LeaveUnreadAnglesExactlyAsTheyWere) {
    const TerrainGenConfig config = ParseConfig(OneLayer(R"({"type": "dunes"})"));
    EXPECT_EQ(std::get<DuneLayerSpec>(config.layers.at(0)).dunes, DuneLayerSpec{}.dunes);
}

TEST(ConfigAngles, WriteDegreesAndRoundTripExactly) {
    TerrainGenConfig config;
    StrataLayerSpec strata;
    strata.strata.dip = 0.03;
    strata.strata.dipAzimuth = 0.8;
    config.layers.push_back(strata);
    const std::string json = ConfigToJson(config);
    const Json layer = Json::parse(json)["layers"][0];
    EXPECT_EQ(layer["dip_deg"].get<double>(), DegreesFromRadians(0.03)) << json;
    EXPECT_EQ(layer["dip_azimuth_deg"].get<double>(), DegreesFromRadians(0.8)) << json;
    EXPECT_EQ(layer.find("dip"), layer.end()) << json;
    EXPECT_EQ(ParseConfig(json), config);
    EXPECT_EQ(ConfigToJson(ParseConfig(json)), json);
    const std::string written =
        ConfigToJson(ParseConfig(OneLayer(R"({"type": "strata", "dip_deg": 12.5})")));
    EXPECT_EQ(Json::parse(written)["layers"][0]["dip_deg"].get<double>(), 12.5) << written;
}

TEST(ConfigAngles, ReadOptionalAnglesAndUnsetThemWithNull) {
    const std::string set = OneLayer(
        R"({"type": "boulders", "ventifacts": {"second_wind_azimuth_deg": 180}, "max_slope_deg": 30})");
    const TerrainGenConfig config = ParseConfig(set);
    const auto& boulders = std::get<terraingen::BoulderLayerSpec>(config.layers.at(0)).boulders;
    ASSERT_TRUE(boulders.ventifacts.secondWindAzimuth.has_value());
    EXPECT_EQ(*boulders.ventifacts.secondWindAzimuth, 180.0 * kDegree);
    EXPECT_EQ(boulders.pose.maxSlope, std::optional<double>(30.0 * kDegree));
    const TerrainGenConfig unset =
        ParseConfig(OneLayer(R"({"type": "boulders", "max_slope_deg": null})"));
    EXPECT_FALSE(std::get<terraingen::BoulderLayerSpec>(unset.layers.at(0)).boulders.pose.maxSlope);
}

TEST(ConfigAngles, CheckRangesInDegreesAtTheKey) {
    EXPECT_EQ(ReadError(OneLayer(R"({"type": "dunes", "slip_face_angle_deg": 33})")), "valid");
    EXPECT_EQ(ReadError(OneLayer(R"({"type": "dunes", "slip_face_angle_deg": 90})")),
              "layers[0].slip_face_angle_deg: must be above 0 and below 90 deg, got 90");
    EXPECT_EQ(ReadError(OneLayer(R"({"type": "strata", "dip_deg": -1})")),
              "layers[0].dip_deg: must be from 0 to below 90 deg, got -1");
}

TEST(ConfigAngles, RejectTheOldRadianKeys) {
    for (const std::string& layer :
         {std::string(R"({"type": "dunes", "slip_face_angle": 0.5})"),
          std::string(R"({"type": "strata", "dip": 0.05})"),
          std::string(R"({"type": "sand_shadow", "wind_azimuth": 0.5})")}) {
        EXPECT_NE(ReadError(OneLayer(layer)).find("unknown key"), std::string::npos) << layer;
    }
}

TEST(ConfigAngles, AreDescribedInDegreesInTheSchema) {
    int angles = 0;
    const auto check = [&](const auto& self, const Json& json) -> void {
        if (json.is_object() && json.find("name") != json.end() &&
            json.find("unit") != json.end()) {
            const std::string name = json["name"];
            const bool suffixed = name.size() > 4 && name.compare(name.size() - 4, 4, "_deg") == 0;
            const Json schema = json.value("schema", Json::object());
            const std::string kind = schema.value("kind", "");
            const bool number = kind == "number" ||
                                (kind == "optional" &&
                                 schema.value("of", Json::object()).value("kind", "") == "number");
            if (number || json["unit"] == "deg") {
                EXPECT_EQ(suffixed, json["unit"] == "deg") << name;
            }
            angles += suffixed ? 1 : 0;
        }
        if (json.is_structured()) {
            for (const auto& item : json.items()) {
                self(self, item.value());
            }
        }
    };
    check(check, Json::parse(terraingen::ConfigSchema()));
    EXPECT_GE(angles, 39);
}

TEST(ConfigAngles, ReadManualBoulderAnglesInDegreesAndLeaveOmittedOnesUnset) {
    const TerrainGenConfig config = ParseConfig(OneLayer(
        R"({"type": "boulders", "manual": [{"x": 1, "y": 2, "diameter": 1.5, "yaw_deg": 30,
            "lean_deg": 10, "tilt_deg": 20, "tilt_azimuth_deg": -90, "burial": 0.3,
            "rest": "side", "shape_seed": 9}, {"x": 0, "y": 0, "diameter": 1, "yaw_deg": null}]})"));
    const auto& manual = std::get<terraingen::BoulderLayerSpec>(config.layers.at(0)).manual;
    ASSERT_EQ(manual.size(), 2u);
    EXPECT_EQ(manual[0].yawDegrees, 30.0);
    EXPECT_EQ(manual[0].leanDegrees, 10.0);
    EXPECT_EQ(manual[0].tiltDegrees, 20.0);
    EXPECT_EQ(manual[0].tiltAzimuthDegrees, -90.0);
    EXPECT_EQ(manual[0].burial, 0.3);
    EXPECT_EQ(manual[0].rest, terraingen::BoulderRest::Side);
    EXPECT_EQ(manual[0].shapeSeed, std::optional<std::uint64_t>(9u));
    EXPECT_TRUE(!manual[0].height && !manual[0].wear);
    const terraingen::ManualBoulder unset{0.0, 0.0, 1.0};
    EXPECT_EQ(manual[1], unset) << "every field unset";
    const Json written = Json::parse(ConfigToJson(config))["layers"][0]["manual"][0];
    EXPECT_EQ(written["yaw_deg"], 30.0);
    EXPECT_EQ(written["tilt_azimuth_deg"], -90.0);
    EXPECT_TRUE(written["height"].is_null());
    EXPECT_EQ(ParseConfig(ConfigToJson(config)), config);
    EXPECT_EQ(terraingen::testing::ParseErrorPath(
                  OneLayer(R"({"type": "boulders", "manual": [{"yaw": 1}]})")),
              "layers[0].manual[0].yaw");
}
