#include "terraingen/app/gui/config_editor.h"
#include "terraingen/config/json.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace {

using Json = terraingen::gui::ConfigEditor::Json;
using terraingen::gui::ConfigEditor;

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

    /// Draws `editor`'s outliner and inspector on `config` for one frame, each in a window;
    /// returns whether either reported a change.
    static bool Frame(ConfigEditor& editor, Json& config) {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(300.0f, 3800.0f));
        ImGui::Begin("Outliner");
        bool changed = editor.DrawOutliner(config);
        ImGui::End();
        ImGui::SetNextWindowSize(ImVec2(600.0f, 3800.0f));
        ImGui::Begin("Inspector");
        changed |= editor.DrawInspector(config);
        ImGui::End();
        ImGui::Render();
        return changed;
    }
};

/// A configuration using every base, layer and output type of the catalog, a chain of bases and an
/// albedo.
Json Everything() {
    const Json catalog = Json::parse(terraingen::ConfigCatalog());
    Json config = Json{{"schema_version", terraingen::kConfigSchemaVersion}};
    config["body"] = catalog["body"].back();
    config["base"] = Json{{"type", "chain"}, {"sources", catalog["base"]}};
    config["layers"] = catalog["layers"];
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
    EXPECT_EQ(editor.NewValue(field("body")), (Json{{"type", "plane"}}));
    EXPECT_TRUE(editor.NewValue(field("base")).is_null());
    EXPECT_EQ(editor.NewValue(field("layers")), Json::array());
    EXPECT_TRUE(editor.NewValue(field("albedo")).is_null());
    EXPECT_EQ(editor.Resolve(field("body"))["kind"], "choice");
    EXPECT_EQ(editor.NewValue(Json{{"kind", "boolean"}}), false);
    EXPECT_EQ(editor.NewValue(Json{{"kind", "string"}}), "");
    EXPECT_EQ(editor.NewValue(Json{{"kind", "integer"}}), 0);
    EXPECT_EQ(editor.NewValue(Json{{"kind", "number"}}), 0.0);
    EXPECT_EQ(editor.NewValue(Json{{"kind", "enum"}, {"choices", {"a", "b"}}}), "a");
    EXPECT_EQ(editor.NewValue(Json{{"kind", "object"}, {"fields", Json::array()}}), Json::object());
}

TEST(ConfigEditor, SwitchingTypeKeepsTheFieldsBothTypesHave) {
    const ConfigEditor editor;
    Json output = Json{{"type", "heightmap"}, {"path", "a.tif"}, {"tile_size", 64}};
    Json outputNode;
    for (const Json& f : editor.Schema()["root"]["fields"]) {
        if (f["name"] == "outputs") {
            outputNode = f["schema"]["of"];
        }
    }
    editor.SetType(output, outputNode, "crater_list");
    EXPECT_EQ(output, (Json{{"type", "crater_list"}, {"path", "a.tif"}}));
    editor.SetType(output, outputNode, "no_such_type");
    EXPECT_EQ(output["type"], "crater_list") << "an unknown type changes nothing";
    Json notAnObject = 5;
    editor.SetType(notAnObject, outputNode, "mesh");
    EXPECT_EQ(notAnObject, (Json{{"type", "mesh"}}));
}

TEST(ConfigEditor, LabelsKeysInWordsWithoutTheirUnit) {
    EXPECT_EQ(ConfigEditor::Label("pixel_size", "m"), "Pixel size");
    EXPECT_EQ(ConfigEditor::Label("age_gyr", "Gyr"), "Age");
    EXPECT_EQ(ConfigEditor::Label("lifetime_gyr"), "Lifetime gyr");
    EXPECT_EQ(ConfigEditor::Label("heading_deg", "deg"), "Heading");
    EXPECT_EQ(ConfigEditor::Label("centre_x"), "Centre X");
    EXPECT_EQ(ConfigEditor::Label("m", "m"), "M") << "a lone word stays";
    EXPECT_EQ(ConfigEditor::Label("rim_smoothing"), "Rim smoothing");
    EXPECT_EQ(ConfigEditor::Label(""), "");
}

TEST(ConfigEditor, AddsRemovesAndMovesItemsWithTheSelectionFollowing) {
    ConfigEditor editor;
    Json config = Json::object();
    EXPECT_FALSE(editor.AddItem(config, "layers", "no_such_type"));
    EXPECT_FALSE(editor.AddItem(config, "body", "sphere")) << "not a list";
    EXPECT_FALSE(editor.AddItem(config, "nothing", "noise"));
    ASSERT_TRUE(editor.AddItem(config, "layers", "noise"));
    ASSERT_TRUE(editor.AddItem(config, "layers", "craters"));
    ASSERT_TRUE(editor.AddItem(config, "layers", "boulders"));
    EXPECT_EQ(config["layers"].size(), 3u);
    EXPECT_EQ(editor.Selection(), "/layers/2") << "a new item is selected";

    EXPECT_TRUE(editor.MoveItem(config, "layers", 2, 0));
    EXPECT_EQ(config["layers"][0]["type"], "boulders");
    EXPECT_EQ(editor.Selection(), "/layers/0") << "the selection follows the moved item";
    editor.Select("/layers/1");
    EXPECT_TRUE(editor.MoveItem(config, "layers", 0, 2));
    EXPECT_EQ(editor.Selection(), "/layers/0") << "items after the moved one shift up";
    EXPECT_TRUE(editor.MoveItem(config, "layers", 2, 0));
    EXPECT_EQ(editor.Selection(), "/layers/1") << "and down";
    EXPECT_FALSE(editor.MoveItem(config, "layers", 0, 3));
    EXPECT_TRUE(editor.MoveItem(config, "layers", 1, 1));

    editor.Select("/layers/2");
    EXPECT_TRUE(editor.RemoveItem(config, "layers", 0));
    EXPECT_EQ(editor.Selection(), "/layers/1");
    EXPECT_TRUE(editor.RemoveItem(config, "layers", 1));
    EXPECT_EQ(editor.Selection(), "") << "removing the selected item clears the selection";
    EXPECT_FALSE(editor.RemoveItem(config, "layers", 5));
    EXPECT_FALSE(editor.RemoveItem(config, "outputs", 0));
    EXPECT_EQ(config["layers"].size(), 1u);
    EXPECT_NE(editor.RootField("albedo"), nullptr);
    EXPECT_EQ(editor.RootField("nothing"), nullptr);
}

TEST_F(HeadlessImGui, DrawsEverySelectionWithoutChangingTheConfiguration) {
    ConfigEditor editor;
    Json config = Everything();
    const Json before = config;
    std::vector<std::string> selections = {"", "/body", "/base", "/albedo"};
    for (const char* list : {"layers", "outputs"}) {
        for (std::size_t i = 0; i < config[list].size(); ++i) {
            selections.push_back("/" + std::string(list) + "/" + std::to_string(i));
        }
    }
    for (const std::string& selection : selections) {
        editor.Select(selection);
        editor.ExpandAll();
        EXPECT_FALSE(Frame(editor, config)) << selection;
        EXPECT_FALSE(Frame(editor, config)) << selection << ", expanded";
        EXPECT_EQ(editor.Selection(), selection);
    }
    EXPECT_EQ(config, before);
    EXPECT_NO_THROW(terraingen::ParseConfig(config.dump()));
    editor.CollapseAll();
    EXPECT_FALSE(Frame(editor, config));

    for (const char* gone : {"/layers/99", "/nothing", "not a pointer", "/albedo"}) {
        Json small = Json{{"schema_version", 1}};
        editor.Select(gone);
        EXPECT_FALSE(Frame(editor, small)) << gone;
        EXPECT_EQ(editor.Selection(), "") << gone << " is no longer selected";
    }
    Json empty = Json::array();
    EXPECT_FALSE(Frame(editor, empty));
    EXPECT_TRUE(empty.is_object()) << "a document that is no object starts over";
}
