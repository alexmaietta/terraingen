#include "configs.h"

#include <gtest/gtest.h>

#include <limits>
#include <optional>
#include <string>
#include <variant>

using terraingen::ConfigToJson;
using terraingen::CraterLayerSpec;
using terraingen::CraterListOutput;
using terraingen::ListFormat;
using terraingen::ParseConfig;
using terraingen::TerrainGenConfig;
using terraingen::testing::ParseErrorPath;

TEST(ConfigJson, ReadsInfiniteBoundsAsUnset) {
    TerrainGenConfig config;
    terraingen::SecondaryLayerSpec secondaries;
    secondaries.secondaries.maxDiameter = 80.0;
    config.layers = {CraterLayerSpec{}, secondaries};
    const std::string json = ConfigToJson(config);
    EXPECT_NE(json.find("\"max_diameter\": 80.0"), std::string::npos) << json;
    EXPECT_NE(json.find("\"max_age_gyr\": null"), std::string::npos) << json;
    EXPECT_NE(json.find("\"min\": null"), std::string::npos) << "an unbounded distribution";
    const TerrainGenConfig parsed = ParseConfig(
        R"({"schema_version": 1, "layers": [{"type": "craters", "apply": {"irregularity":
            {"max_diameter": 30, "roughness": {"min": -1, "max": null}}}}]})");
    const auto& irregularity = std::get<CraterLayerSpec>(parsed.layers[0]).apply.irregularity;
    EXPECT_EQ(irregularity.selection.maxDiameter, 30.0);
    EXPECT_EQ(irregularity.roughness.min, -1.0);
    EXPECT_EQ(irregularity.roughness.max, std::numeric_limits<double>::infinity());
    EXPECT_EQ(irregularity.selection.maxAgeGyr, std::numeric_limits<double>::infinity());
}

TEST(ConfigJson, OmittedKeysTakeDefaultsAndOmittedListsAreEmpty) {
    const TerrainGenConfig minimal = ParseConfig(R"({"schema_version": 1})");
    EXPECT_FALSE(minimal.source.has_value());
    EXPECT_TRUE(std::holds_alternative<terraingen::PlaneSurfaceSpec>(minimal.body));
    EXPECT_TRUE(minimal.layers.empty());
    EXPECT_TRUE(minimal.outputs.empty());
    const TerrainGenConfig partial = ParseConfig(R"({
        "schema_version": 1,
        "layers": [{"type": "noise", "stack": [{"octaves": 3, "hurst": 0.7}]},
                   {"type": "craters", "density": {"type": "trask"}}],
        "outputs": [{"type": "heightmap", "path": "h.tif", "extent_m": {"size": [64, 32]}}]
    })");
    const auto& area = std::get<terraingen::HeightmapOutput>(partial.outputs.at(0)).area;
    EXPECT_EQ(area.extentM->size, (std::vector<double>{64.0, 32.0}));
    EXPECT_TRUE(area.extentM->center.empty());
    EXPECT_FALSE(area.pixelSize);
    EXPECT_FALSE(area.extentDeg);
    ASSERT_EQ(partial.layers.size(), 2u);
    const auto& noise = std::get<terraingen::NoiseLayerSpec>(partial.layers[0]);
    EXPECT_EQ(noise.stack.at(0).layer.fractal.octaves, 3);
    EXPECT_EQ(noise.stack.at(0).hurst, 0.7f);
    const auto& craters = std::get<CraterLayerSpec>(partial.layers[1]);
    EXPECT_TRUE(std::holds_alternative<terraingen::TraskSpec>(craters.density));
    EXPECT_EQ(craters.minDiameter, 2.5);
    EXPECT_EQ(craters.apply.rimSmoothing, terraingen::RimSmoothing{});
}

TEST(ConfigJson, ReadsBoulderLayersWithOptionalExposure) {
    const TerrainGenConfig config = ParseConfig(R"({"schema_version": 1, "layers": [
        {"type": "boulders", "coverage": 0.04, "relief": "none", "ejecta": {"min_crater_diameter": 20},
         "exposure": {"reach": 6}},
        {"type": "boulders", "exposure": null}]})");
    ASSERT_EQ(config.layers.size(), 2u);
    const auto& boulders = std::get<terraingen::BoulderLayerSpec>(config.layers[0]).boulders;
    EXPECT_EQ(boulders.sizes.coverage, 0.04);
    EXPECT_EQ(boulders.rendering.relief, terraingen::BoulderRelief::None);
    EXPECT_EQ(boulders.ejecta.minCraterDiameter, 20.0);
    EXPECT_EQ(boulders.ejecta.coverage, terraingen::BoulderEjecta{}.coverage);
    ASSERT_TRUE(boulders.bedrock.exposure.has_value());
    EXPECT_EQ(boulders.bedrock.exposure->reach, 6.0);
    EXPECT_EQ(boulders.bedrock.exposure->riseBoulder, terraingen::ExposureParams{}.riseBoulder);
    EXPECT_FALSE(
        std::get<terraingen::BoulderLayerSpec>(config.layers[1]).boulders.bedrock.exposure);
    EXPECT_EQ(ParseErrorPath(
                  R"({"schema_version": 1, "layers": [{"type": "boulders", "relief": "mesh"}]})"),
              "layers[0].relief");
    EXPECT_EQ(
        ParseErrorPath(
            R"({"schema_version": 1, "layers": [{"type": "boulders", "cache_boulders": 1}]})"),
        "layers[0].cache_boulders");
    EXPECT_EQ(
        ParseErrorPath(
            R"({"schema_version": 1, "layers": [{"type": "boulders", "exposure": {"x": 1}}]})"),
        "layers[0].exposure.x");
}

TEST(ConfigJson, ReadsTransformsAndNestedRegions) {
    const TerrainGenConfig config = ParseConfig(R"({"schema_version": 1, "layers": [
        {"type": "scale", "scale": 2},
        {"type": "clamp", "min": -1},
        {"type": "region", "shape": {"type": "circle", "radius": 30}, "feather": 5,
         "layer": {"type": "region", "shape": {"type": "rectangle", "max_x": 1, "max_y": 2},
                   "layer": {"type": "clamp", "max": 0}}}]})");
    ASSERT_EQ(config.layers.size(), 3u);
    EXPECT_EQ(std::get<terraingen::ScaleLayerSpec>(config.layers[0]),
              (terraingen::ScaleLayerSpec{2.0, 0.0}));
    EXPECT_EQ(std::get<terraingen::ClampLayerSpec>(config.layers[1]),
              (terraingen::ClampLayerSpec{-1.0, std::nullopt}));
    const auto& outer = std::get<terraingen::RegionLayerSpec>(config.layers[2]);
    EXPECT_EQ(outer.shape,
              (terraingen::RegionShapeSpec{terraingen::CircleRegionSpec{0.0, 0.0, 30.0}}));
    EXPECT_EQ(outer.feather, 5.0);
    EXPECT_EQ(outer.blend, terraingen::BlendShape::Smoothstep);
    const auto& inner = std::get<terraingen::RegionLayerSpec>(*outer.layer);
    EXPECT_EQ(std::get<terraingen::RectangleRegionSpec>(inner.shape).maxY, 2.0);
    EXPECT_EQ(std::get<terraingen::ClampLayerSpec>(*inner.layer).max, 0.0);
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "layers": [
        {"type": "region", "layer": {"type": "clamp", "mid": 0}}]})"),
              "layers[0].layer.mid");
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "layers": [
        {"type": "region", "shape": {"type": "polygon"}}]})"),
              "layers[0].shape.type");
}

TEST(ConfigJson, InfersTheListFormatFromTheExtension) {
    const TerrainGenConfig config = ParseConfig(R"({"schema_version": 1, "outputs": [
        {"type": "crater_list", "path": "a.json"},
        {"type": "crater_list", "path": "b.csv"},
        {"type": "crater_list", "path": "c.json", "format": "csv"},
        {"type": "boulder_list", "path": "d.json"},
        {"type": "boulder_list", "path": "e.txt"}]})");
    ASSERT_EQ(config.outputs.size(), 5u);
    EXPECT_EQ(std::get<CraterListOutput>(config.outputs[0]).format, ListFormat::Json);
    EXPECT_EQ(std::get<CraterListOutput>(config.outputs[1]).format, ListFormat::Csv);
    EXPECT_EQ(std::get<CraterListOutput>(config.outputs[2]).format, ListFormat::Csv);
    EXPECT_EQ(std::get<terraingen::BoulderListOutput>(config.outputs[3]).format, ListFormat::Json);
    EXPECT_EQ(std::get<terraingen::BoulderListOutput>(config.outputs[4]).format, ListFormat::Csv);
}

TEST(ConfigJson, ReadsDuneLayersByShapeAndRipples) {
    const TerrainGenConfig config = ParseConfig(R"({"schema_version": 1, "layers": [
        {"type": "dunes", "shape": "barchanoid", "wavelength": 300, "height": 18,
         "anchor_x": 137.4, "anchor_y": -4.6,
         "defect_density": 0.003,
         "ripples": {"wavelength": 2.1, "height": 0.2, "defect_density": 0.25}}]})");
    const auto& dunes = std::get<terraingen::DuneLayerSpec>(config.layers.at(0)).dunes;
    EXPECT_EQ(dunes.shape, terraingen::DuneShape::Barchanoid);
    EXPECT_EQ(dunes.wavelength, 300.0);
    EXPECT_EQ(dunes.anchor.x, 137.4);
    EXPECT_EQ(dunes.anchor.y, -4.6);
    EXPECT_EQ(dunes.ripples.wavelength, 2.1);
    EXPECT_EQ(dunes.ripples.defectDensity, 0.25);
    EXPECT_EQ(dunes.defectDensity, 0.003);
    EXPECT_EQ(dunes.slipFaceAngle, terraingen::DuneOptions{}.slipFaceAngle) << "defaults kept";
    EXPECT_THROW(
        ParseConfig(R"({"schema_version": 1, "layers": [{"type": "dunes", "shape": "star"}]})"),
        terraingen::ConfigError);
}
