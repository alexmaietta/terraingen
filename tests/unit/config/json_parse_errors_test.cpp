// SPDX-License-Identifier: BSD-3-Clause

#include "configs.h"

#include <gtest/gtest.h>

#include <string>

using terraingen::testing::ParseErrorMessage;
using terraingen::testing::ParseErrorPath;

namespace {

/// A configuration JSON and the path of the error parsing it reports.
struct ParseCase {
    const char* name;
    const char* json;
    const char* path;
};

const ParseCase kParseCases[] = {
    {"Truncated", "{", ""},
    {"NotAnObject", "[]", ""},
    {"NoSchemaVersion", "{}", "schema_version"},
    {"NewerSchemaVersion", R"({"schema_version": 2})", "schema_version"},
    {"NewerVersionWithNewKeys", R"({"schema_version": 2, "terrain": {}})", "schema_version"},
    {"SchemaVersionAsString", R"({"schema_version": "1"})", "schema_version"},
    {"HugeSchemaVersion", R"({"schema_version": 3000000000})", "schema_version"},
    {"NegativeSchemaVersion", R"({"schema_version": -3000000000})", "schema_version"},
    {"UnknownRootKey", R"({"schema_version": 1, "color": 3})", "color"},
    {"UnknownRootObject", R"({"schema_version": 1, "grid": {}})", "grid"},
    {"RetiredCratersKey", R"({"schema_version": 1, "craters": {}})", "craters"},
    {"ExtentAsArray",
     R"({"schema_version": 1, "outputs": [{"type": "heightmap", "extent_m": [0, 1]}]})",
     "outputs[0].extent_m"},
    {"ExtentCenterAsNumber",
     R"({"schema_version": 1, "outputs": [{"type": "heightmap", "extent_m": {"center": 1}}]})",
     "outputs[0].extent_m.center"},
    {"ExtentSizeOfStrings",
     R"({"schema_version": 1, "outputs": [{"type": "heightmap", "extent_m": {"size": ["a", 2]}}]})",
     "outputs[0].extent_m.size[0]"},
    {"ExtentCenterOfOneNumber",
     R"({"schema_version": 1, "outputs": [{"type": "heightmap", "extent_m": {"center": [1]}}]})",
     "outputs[0].extent_m.center"},
    {"PixelSizeAsString",
     R"({"schema_version": 1, "outputs": [{"type": "heightmap", "pixel_size_m": "1"}]})",
     "outputs[0].pixel_size_m"},
    {"PixelSizeOverflow",
     R"({"schema_version": 1, "outputs": [{"type": "heightmap", "pixel_size_m": 1e400}]})", ""},
    {"RemovedRegion",
     R"({"schema_version": 1, "outputs": [{"type": "heightmap", "region": {"width": 4}}]})",
     "outputs[0].region"},
    {"ReportWithArea",
     R"({"schema_version": 1, "outputs": [{"type": "report", "pixel_size_m": 1}]})",
     "outputs[0].pixel_size_m"},
    {"SourceAsArray", R"({"schema_version": 1, "source": []})", "source"},
    {"RemovedBaseKey", R"({"schema_version": 1, "base": {"type": "raster", "path": "x.tif"}})",
     "base"},
    {"UnknownSourceType", R"({"schema_version": 1, "source": {"type": "flat"}})", "source.type"},
    {"SourceWithoutType", R"({"schema_version": 1, "source": {"path": "x.tif"}})", "source.type"},
    {"RemovedResampling",
     R"({"schema_version": 1, "source": {"type": "raster", "resampling": "cubic"}})",
     "source.resampling"},
    {"UpsamplingAsNumber",
     R"({"schema_version": 1, "source": {"type": "raster", "upsampling": 2}})",
     "source.upsampling"},
    {"UnknownInterp",
     R"({"schema_version": 1, "source": {"type": "raster",
         "upsampling": {"type": "diamond_square", "interp": "cubic"}}})",
     "source.upsampling.interp"},
    {"UnknownSyntheticKey",
     R"({"schema_version": 1, "source": {"type": "synthetic", "elevation": 3}})",
     "source.elevation"},
    {"UnknownRasterKey", R"({"schema_version": 1, "source": {"type": "raster", "layers": []}})",
     "source.layers"},
    {"StackAsObject", R"({"schema_version": 1, "layers": [{"type": "noise", "stack": {}}]})",
     "layers[0].stack"},
    {"RetiredLayerType", R"({"schema_version": 1, "layers": [{"type": "rocks"}]})",
     "layers[0].type"},
    {"UnknownRimSmoothingKey",
     R"({"schema_version": 1, "layers": [{"type": "craters", "apply": {"rim_smoothing": {"sigma": 1}}}]})",
     "layers[0].apply.rim_smoothing.sigma"},
    {"UnknownCompression",
     R"({"schema_version": 1, "outputs": [{"type": "heightmap", "compression": "zstd"}]})",
     "outputs[0].compression"},
    {"UnknownNoiseKey",
     R"({"schema_version": 1, "layers": [{"type": "noise", "stack": [{}, {"octave": 3}]}]})",
     "layers[0].stack[1].octave"},
    {"UnknownBasis",
     R"({"schema_version": 1, "layers": [{"type": "noise", "stack": [{"basis": "curl"}]}]})",
     "layers[0].stack[0].basis"},
    {"FloatOverflow",
     R"({"schema_version": 1, "layers": [{"type": "noise", "stack": [{"amplitude_m": 1e300}]}]})",
     "layers[0].stack[0].amplitude_m"},
    {"IntegerOverflow",
     R"({"schema_version": 1, "layers": [{"type": "noise", "stack": [{"octaves": 1e12}]}]})",
     "layers[0].stack[0].octaves"},
    {"SeedOverflow",
     R"({"schema_version": 1, "layers": [{"type": "noise", "stack": [{"seed": 5000000000}]}]})",
     "layers[0].stack[0].seed"},
    {"EnabledAsNumber",
     R"({"schema_version": 1, "layers": [{"type": "noise", "stack": [{"enabled": 1}]}]})",
     "layers[0].stack[0].enabled"},
    {"RenamedCenterHeightDatum",
     R"({"schema_version": 1, "layers": [{"type": "craters", "apply": {"datum": "centre_height"}}]})",
     "layers[0].apply.datum"},
    {"RenamedMetersPerUnit",
     R"({"schema_version": 1, "body": {"type": "shape_model", "path": "x.icq", "metres_per_unit": 1000}})",
     "body.metres_per_unit"},
    {"DensityWithoutType",
     R"({"schema_version": 1, "layers": [{"type": "craters", "density": {"age_gyr": 3}}]})",
     "layers[0].density.type"},
    {"MisspelledDensity",
     R"({"schema_version": 1, "layers": [{"type": "craters", "density": {"type": "hartman"}}]})",
     "layers[0].density.type"},
    {"DensityAsString",
     R"({"schema_version": 1, "layers": [{"type": "craters", "density": "trask"}]})",
     "layers[0].density"},
    {"KeyOfAnotherDensity",
     R"({"schema_version": 1, "layers": [{"type": "craters", "density": {"type": "trask", "age_gyr": 3}}]})",
     "layers[0].density.age_gyr"},
    {"RemovedCraterGenerateKey",
     R"({"schema_version": 1, "layers": [{"type": "craters", "generate": false}]})",
     "layers[0].generate"},
    {"BoulderGenerateKey",
     R"({"schema_version": 1, "layers": [{"type": "boulders", "generate": false}]})",
     "layers[0].generate"},
    {"GenerateRandomKeys",
     R"({"schema_version": 1, "layers": [{"type": "craters", "generate_random": false}, {"type": "boulders", "generate_random": false}]})",
     "no error"},
    {"UnknownManualKey",
     R"({"schema_version": 1, "layers": [{"type": "craters", "manual": [{"x": 1, "pinned": true}]}]})",
     "layers[0].manual[0].pinned"},
    {"AgesTypeAsNumber",
     R"({"schema_version": 1, "layers": [{"type": "craters", "ages": {"type": 3}}]})",
     "layers[0].ages.type"},
    {"UnknownOutputType", R"({"schema_version": 1, "outputs": [{"type": "body_mesh"}]})",
     "outputs[0].type"},
    {"FormatAsNumber",
     R"({"schema_version": 1, "outputs": [{"type": "crater_list", "format": 1}]})",
     "outputs[0].format"},
    {"OutputsAsObject", R"({"schema_version": 1, "outputs": {}})", "outputs"},
    {"NullHurst",
     R"({"schema_version": 1, "layers": [{"type": "noise", "stack": [{"hurst": null}]}]})",
     "no error"},
    {"NullDepthRatio",
     R"({"schema_version": 1, "layers": [{"type": "craters", "degradation": {"type": "none", "depth_ratio": null}}]})",
     "no error"},
};

class ParseConfigPaths : public ::testing::TestWithParam<ParseCase> {};

}  // namespace

TEST_P(ParseConfigPaths, ReportsTheProblemByItsJsonPath) {
    EXPECT_EQ(ParseErrorPath(GetParam().json), GetParam().path) << GetParam().json;
}

INSTANTIATE_TEST_SUITE_P(Json, ParseConfigPaths, ::testing::ValuesIn(kParseCases),
                         [](const ::testing::TestParamInfo<ParseCase>& param) {
                             return std::string(param.param.name);
                         });

TEST(ConfigJson, ErrorMessagesNameThePathAndTheAlternatives) {
    const std::string message = ParseErrorMessage(
        R"({"schema_version": 1, "layers": [{"type": "craters", "profile": {"type": "bowl"}}]})");
    EXPECT_EQ(message.rfind("layers[0].profile.type: unknown type \"bowl\"", 0), 0u) << message;
    EXPECT_NE(message.find("fassett_thomson, martin_parkes_dunstan"), std::string::npos);
    EXPECT_EQ(ParseErrorMessage(R"({"schema_version": 2})"),
              "schema_version: version 2 is not supported; this library reads version 1");
    EXPECT_EQ(ParseErrorMessage("{}"), "schema_version: required; this library reads version 1");
    EXPECT_EQ(ParseErrorMessage(R"({"schema_version": 1, "color": 3})")
                  .rfind("color: unknown key; expected one of: ", 0),
              0u);
    EXPECT_EQ(ParseErrorMessage("[]"), "expected an object, not array");
}

TEST(ConfigJson, ReportsAMissingRequiredKeyAfterAnyUnknownKey) {
    EXPECT_EQ(ParseErrorMessage(R"({"schema_version": 1, "body": {"type": "sphere"}})"),
              "body.radius_m: required");
    EXPECT_EQ(ParseErrorMessage(R"({"schema_version": 1, "body": {"type": "ellipsoid"}})"),
              "body.radii_m: required");
    EXPECT_EQ(ParseErrorMessage(R"({"schema_version": 1, "source": {"type": "raster"}})"),
              "source.path: required");
    EXPECT_EQ(ParseErrorMessage(R"({"schema_version": 1, "layers": [{"type": "secondaries"}]})"),
              "layers[0].onset_count: required");
    EXPECT_EQ(ParseErrorMessage(R"({"schema_version": 1, "body": {"type": "sphere", "radius": 3}})")
                  .rfind("body.radius: unknown key", 0),
              0u)
        << "a misspelled key is reported before the key it stands for";
}
