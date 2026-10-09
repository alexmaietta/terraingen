#include "terraingen/app/viewer/config_editor.h"
#include "terraingen/pipeline/config_json.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <string>

namespace {

using Json = nlohmann::json;
using terraingen::viewer::ConfigEditor;

/// An ImGui context without a window or renderer, for drawing widgets in tests.
class HeadlessImGui : public ::testing::Test {
  protected:
    void SetUp() override {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280.0f, 4000.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }

    void TearDown() override {
        ImGui::DestroyContext();
    }

    /// Draws `editor` on `config` for one frame in a window; returns whether it reported a change.
    static bool Frame(ConfigEditor& editor, Json& config) {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(900.0f, 3800.0f));
        ImGui::Begin("Editor");
        const bool changed = editor.Draw(config);
        ImGui::End();
        ImGui::Render();
        return changed;
    }
};

/// A configuration using every source, layer, filter and output type of the catalog, a chain of
/// sources and an albedo.
Json Everything() {
    const Json catalog = Json::parse(terraingen::ConfigCatalog());
    Json config = Json{{"schema_version", terraingen::kConfigSchemaVersion}};
    config["surface"] = catalog["surface"].back();
    config["source"] = Json{{"type", "chain"}, {"sources", catalog["source"]}};
    config["layers"] = catalog["layers"];
    config["filters"] = catalog["filters"];
    config["outputs"] = catalog["outputs"];
    config["albedo"] = catalog["albedo"];
    return config;
}

}  // namespace

TEST(ConfigEditor, MakesNewValuesFromTheSchema) {
    const ConfigEditor editor;
    const Json& root = editor.Schema()["root"];
    const auto field = [&](const std::string& name) {
        for (const Json& f : root["fields"]) {
            if (f["name"] == name) {
                return f["schema"];
            }
        }
        return Json();
    };
    EXPECT_EQ(editor.NewValue(field("source")), (Json{{"type", "flat"}}));
    EXPECT_EQ(editor.NewValue(field("layers")), Json::array());
    EXPECT_TRUE(editor.NewValue(field("albedo")).is_null());
    EXPECT_EQ(editor.Resolve(field("source"))["kind"], "choice");
    EXPECT_EQ(editor.NewValue(Json{{"kind", "boolean"}}), false);
    EXPECT_EQ(editor.NewValue(Json{{"kind", "string"}}), "");
    EXPECT_EQ(editor.NewValue(Json{{"kind", "integer"}}), 0);
    EXPECT_EQ(editor.NewValue(Json{{"kind", "number"}}), 0.0);
    EXPECT_EQ(editor.NewValue(Json{{"kind", "enum"}, {"choices", {"a", "b"}}}), "a");
    EXPECT_EQ(editor.NewValue(Json{{"kind", "object"}, {"fields", Json::array()}}), Json::object());
}

TEST(ConfigEditor, SwitchingTypeKeepsTheFieldsBothTypesHave) {
    const ConfigEditor editor;
    Json source = Json{{"type", "flat"}, {"pixel_size", 3.0}, {"width", 65}};
    Json sourceNode;
    for (const Json& f : editor.Schema()["root"]["fields"]) {
        if (f["name"] == "source") {
            sourceNode = f["schema"];
        }
    }
    editor.SetType(source, sourceNode, "raster");
    EXPECT_EQ(source, (Json{{"type", "raster"}, {"pixel_size", 3.0}}));
    editor.SetType(source, sourceNode, "no_such_type");
    EXPECT_EQ(source["type"], "raster") << "an unknown type changes nothing";
    Json notAnObject = 5;
    editor.SetType(notAnObject, sourceNode, "chain");
    EXPECT_EQ(notAnObject, (Json{{"type", "chain"}}));
}

TEST_F(HeadlessImGui, DrawsEveryTypeWithoutChangingTheConfiguration) {
    ConfigEditor editor;
    Json config = Everything();
    const Json before = config;
    editor.ExpandAll();
    EXPECT_FALSE(Frame(editor, config));
    EXPECT_FALSE(Frame(editor, config)) << "the expanded tree draws every widget";
    EXPECT_EQ(config, before);
    EXPECT_NO_THROW(terraingen::ParseConfig(config.dump()));
    editor.CollapseAll();
    EXPECT_FALSE(Frame(editor, config));

    Json empty = Json::array();
    EXPECT_FALSE(Frame(editor, empty));
    EXPECT_EQ(empty, (Json{{"schema_version", terraingen::kConfigSchemaVersion}}))
        << "a document that is no object starts over, with only the schema version";
}
