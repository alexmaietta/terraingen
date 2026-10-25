// SPDX-License-Identifier: BSD-3-Clause

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
using terraingen::testing::SpecJson;

TEST(ConfigJson, ReadsInfiniteBoundsAsUnset) {
    TerrainGenConfig config;
    terraingen::SecondaryLayerSpec secondaries;
    secondaries.secondaries.maxDiameter = 80.0;
    config.layers = {CraterLayerSpec{}, secondaries};
    const std::string json = ConfigToJson(config);
    EXPECT_NE(json.find("\"max_diameter_m\": 80.0"), std::string::npos) << json;
    EXPECT_NE(json.find("\"max_age_gyr\": null"), std::string::npos) << json;
    EXPECT_NE(json.find("\"min\": null"), std::string::npos) << "an unbounded distribution";
    const TerrainGenConfig parsed = ParseConfig(
        R"({"schema_version": 1, "layers": [{"type": "craters", "apply": {"irregularity":
            {"max_diameter_m": 30, "roughness": {"min": -1, "max": null}}}}]})");
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
        {"type": "boulders", "coverage": 0.04, "relief": "none",
         "ejecta": {"min_crater_diameter_m": 20},
         "exposure": {"reach_m": 6}},
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

TEST(ConfigJson, ReadsTransformsAndNestedMaskedLayers) {
    const TerrainGenConfig config = ParseConfig(R"({"schema_version": 1, "layers": [
        {"type": "scale", "scale": 2},
        {"type": "clamp", "min_m": -1},
        {"type": "masked", "shape": {"type": "circle", "radius_m": 30}, "feather_m": 5,
         "layer": {"type": "masked", "shape": {"type": "rectangle", "max_x": 1, "max_y": 2},
                   "layer": {"type": "clamp", "max_m": 0}}}]})");
    ASSERT_EQ(config.layers.size(), 3u);
    EXPECT_EQ(SpecJson(std::get<terraingen::ScaleLayerSpec>(config.layers[0])),
              SpecJson(terraingen::ScaleLayerSpec{2.0, 0.0}));
    EXPECT_EQ(SpecJson(std::get<terraingen::ClampLayerSpec>(config.layers[1])),
              SpecJson(terraingen::ClampLayerSpec{-1.0, std::nullopt}));
    const auto& outer = std::get<terraingen::MaskedLayerSpec>(config.layers[2]);
    EXPECT_EQ(SpecJson(outer.shape),
              SpecJson(terraingen::RegionShapeSpec{terraingen::CircleRegionSpec{0.0, 0.0, 30.0}}));
    EXPECT_EQ(outer.feather, 5.0);
    EXPECT_EQ(outer.blend, terraingen::BlendShape::Smoothstep);
    const auto& inner = std::get<terraingen::MaskedLayerSpec>(*outer.layer);
    EXPECT_EQ(std::get<terraingen::RectangleRegionSpec>(inner.shape).maxY, 2.0);
    EXPECT_EQ(std::get<terraingen::ClampLayerSpec>(*inner.layer).max, 0.0);
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "layers": [
        {"type": "masked", "layer": {"type": "clamp", "mid": 0}}]})"),
              "layers[0].layer.mid");
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "layers": [
        {"type": "masked", "shape": {"type": "polygon"}}]})"),
              "layers[0].shape.type");
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "layers": [
        {"type": "region", "layer": {"type": "clamp"}}]})"),
              "layers[0].type")
        << "the old name is rejected";
}

TEST(ConfigJson, ReadsSizeKeysWithUnitsAndFadePixels) {
    const TerrainGenConfig config = ParseConfig(R"({"schema_version": 1, "layers": [
        {"type": "craters", "min_diameter_m": 3, "max_diameter_m": 90,
         "apply": {"fade_px": [1.5, 6], "fade_shape": "linear"},
         "density_map": {"min_diameter_m": 2, "max_diameter_m": 50},
         "source_resolves_px": null},
        {"type": "secondaries", "onset_count": 3, "min_diameter_m": 0.5, "max_diameter_m": 20,
         "min_primary_diameter_m": 100, "max_primary_diameter_m": 900, "source_resolves_px": 6},
        {"type": "boulders", "min_diameter_m": 0.2, "max_diameter_m": 4, "fade_px": [1, 3],
         "ground_spacing_m": 0.5, "bedrock_mean_size_m": 30, "roll_distance_m": 8,
         "patches": {"size_m": 40}, "clusters": {"center_spacing_m": 30, "radius_m": 5},
         "ejecta": {"min_crater_diameter_m": 15, "regolith_thickness_m": 4},
         "fragmentation": {"min_diameter_m": 1.5},
         "model_threshold": {"min_diameter_m": 1, "max_diameter_m": 3, "min_px": 4, "max_px": 40},
         "relief_range": {"min_diameter_m": 0.1, "max_diameter_m": 2},
         "slope_map": {"reference_diameter_m": 2}, "source_resolves_px": 5},
        {"type": "noise", "source_resolves_px": 8},
        {"type": "dunes", "source_resolves_px": null},
        {"type": "bedforms", "source_resolves_px": 2}]})");
    const auto& craters = std::get<CraterLayerSpec>(config.layers[0]);
    EXPECT_EQ(craters.minDiameter, 3.0);
    EXPECT_EQ(craters.maxDiameter, 90.0);
    EXPECT_EQ(craters.apply.fade.startSamples, 1.5);
    EXPECT_EQ(craters.apply.fade.endSamples, 6.0);
    EXPECT_EQ(craters.apply.fade.shape, terraingen::BlendShape::Linear);
    EXPECT_EQ(craters.densityMap->minDiameter, 2.0);
    EXPECT_EQ(craters.densityMap->maxDiameter, 50.0);
    EXPECT_FALSE(craters.sourceResolvesPixels) << "null holds none";
    const auto& secondaries = std::get<terraingen::SecondaryLayerSpec>(config.layers[1]);
    EXPECT_EQ(secondaries.secondaries.minDiameter, 0.5);
    EXPECT_EQ(secondaries.secondaries.maxDiameter, 20.0);
    EXPECT_EQ(secondaries.secondaries.selection.minDiameter, 100.0);
    EXPECT_EQ(secondaries.secondaries.selection.maxDiameter, 900.0);
    EXPECT_EQ(secondaries.secondaries.sourceResolvesPixels, 6.0);
    const auto& spec = std::get<terraingen::BoulderLayerSpec>(config.layers[2]);
    const terraingen::BoulderLayerOptions& boulders = spec.boulders;
    EXPECT_EQ(boulders.sizes.minDiameter, 0.2);
    EXPECT_EQ(boulders.sizes.maxDiameter, 4.0);
    EXPECT_EQ(boulders.fade.startSamples, 1.0);
    EXPECT_EQ(boulders.fade.endSamples, 3.0);
    EXPECT_EQ(boulders.groundSpacing, 0.5);
    EXPECT_EQ(boulders.bedrock.meanSize, 30.0);
    EXPECT_EQ(boulders.pose.rollDistance, 8.0);
    EXPECT_EQ(boulders.patches.size, 40.0);
    EXPECT_EQ(boulders.clusters.centerSpacing, 30.0);
    EXPECT_EQ(boulders.clusters.radius, 5.0);
    EXPECT_EQ(boulders.ejecta.minCraterDiameter, 15.0);
    EXPECT_EQ(boulders.ejecta.regolithThickness, 4.0);
    ASSERT_TRUE(boulders.fragmentation.has_value());
    EXPECT_EQ(boulders.fragmentation->minDiameter, 1.5);
    EXPECT_EQ(boulders.rendering.modelThreshold.minDiameter, 1.0);
    EXPECT_EQ(boulders.rendering.modelThreshold.maxDiameter, 3.0);
    EXPECT_EQ(boulders.rendering.modelThreshold.minPixels, 4.0);
    EXPECT_EQ(boulders.rendering.modelThreshold.maxPixels, 40.0);
    EXPECT_EQ(boulders.rendering.reliefRange.minDiameter, 0.1);
    EXPECT_EQ(boulders.rendering.reliefRange.maxDiameter, 2.0);
    EXPECT_EQ(spec.slopeMap->referenceDiameter, 2.0);
    EXPECT_EQ(boulders.sourceResolvesPixels, 5.0);
    EXPECT_EQ(std::get<terraingen::NoiseLayerSpec>(config.layers[3]).sourceResolvesPixels, 8.0);
    EXPECT_FALSE(std::get<terraingen::DuneLayerSpec>(config.layers[4]).dunes.sourceResolvesPixels);
    EXPECT_EQ(
        std::get<terraingen::BedformLayerSpec>(config.layers[5]).bedforms.sourceResolvesPixels,
        2.0);
    EXPECT_EQ(ParseConfig(ConfigToJson(config)), config);
}

TEST(ConfigJson, RejectsTheOldSizeAndFadeKeys) {
    for (const char* layer :
         {R"({"type": "craters", "min_diameter": 3})", R"({"type": "craters", "max_diameter": 3})",
          R"({"type": "craters", "unresolved_pixels_across": 4})",
          R"({"type": "boulders", "min_diameter": 0.5})",
          R"({"type": "boulders", "ground_spacing": 0.5})",
          R"({"type": "boulders", "fade": {"start_samples": 1}})",
          R"({"type": "secondaries", "min_primary_diameter": 10})"}) {
        const std::string text = std::string(R"({"schema_version": 1, "layers": [)") + layer + "]}";
        EXPECT_EQ(ParseErrorPath(text).rfind("layers[0].", 0), 0u) << layer;
    }
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "layers": [
        {"type": "craters", "apply": {"fade": {"start_samples": 1}}}]})"),
              "layers[0].apply.fade");
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "layers": [
        {"type": "boulders", "fade_px": [1, 2, 3]}]})"),
              "layers[0].fade_px");
    EXPECT_EQ(ParseErrorPath(R"({"schema_version": 1, "layers": [
        {"type": "boulders", "fade_px": [1, "a"]}]})"),
              "layers[0].fade_px[1]");
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
        {"type": "dunes", "shape": "barchanoid", "wavelength_m": 300, "height_m": 18,
         "anchor_x": 137.4, "anchor_y": -4.6,
         "defect_density": 0.003,
         "ripples": {"wavelength_m": 2.1, "height_m": 0.2, "defect_density": 0.25}}]})");
    const auto& dunes = std::get<terraingen::DuneLayerSpec>(config.layers.at(0)).dunes;
    EXPECT_EQ(dunes.shape, terraingen::DuneShape::Barchanoid);
    EXPECT_EQ(dunes.wavelength, 300.0);
    EXPECT_EQ(dunes.anchor.x, 137.4);
    EXPECT_EQ(dunes.anchor.y, -4.6);
    EXPECT_EQ(dunes.ripples->wavelength, 2.1);
    EXPECT_EQ(dunes.ripples->defectDensity, 0.25);
    EXPECT_EQ(dunes.defectDensity, 0.003);
    EXPECT_EQ(dunes.slipFaceAngleDegrees, terraingen::DuneOptions{}.slipFaceAngleDegrees)
        << "defaults kept";
    EXPECT_THROW(
        ParseConfig(R"({"schema_version": 1, "layers": [{"type": "dunes", "shape": "star"}]})"),
        terraingen::ConfigError);
}
