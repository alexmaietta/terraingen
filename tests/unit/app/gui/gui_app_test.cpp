// SPDX-License-Identifier: BSD-3-Clause

#include "support/temp_path.h"
#include "terraingen/app/gui/gui_app.h"
#include "terraingen/app/gui/terrain_scene.h"
#include "terraingen/config/json.h"
#include "terraingen/raster/geotiff/geotiff.h"

#include <ernie/scene/tokens.h>
#include <ernie/util/tensor.h>
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace tokens = ernie::tokens;
using terraingen::gui::GuiApp;
using terraingen::gui::GuiAppOptions;
using terraingen::gui::kTerrainPrim;

const char* const kConfig = R"({
    "schema_version": 1,
    "outputs": [{"type": "heightmap", "path": "terrain.tif",
                 "pixel_size_m": 2, "extent_m": {"center": [0, 0], "size": [130, 66]}}],
    "layers": [{"type": "noise", "stack": [{"wavelength": 40, "amplitude": 3, "octaves": 3}]}]
})";

std::filesystem::path Directory(const std::string& name) {
    return terraingen::testing::TempDirectory("terraingen_gui_" + name);
}

/// Steps `app` until no generation runs, for at most a minute.
void Settle(GuiApp& app) {
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::minutes(1);
    while (app.Generating() && std::chrono::steady_clock::now() < give_up) {
        app.Step();
    }
    app.Step();
}

std::vector<std::int64_t> HeightsShape(GuiApp& app) {
    const ernie::Tensor* heights = app.Scene().Read(kTerrainPrim, tokens::kHeights);
    return heights != nullptr ? heights->Shape() : std::vector<std::int64_t>{};
}

}  // namespace

TEST(GuiApp, OpensAndGeneratesAConfigThenSavesIt) {
    const std::filesystem::path dir = Directory("generate");
    std::ofstream(dir / "terrain.json") << kConfig;
    GuiApp app({dir / "terrain.json", 2, true});
    Settle(app);
    EXPECT_EQ(HeightsShape(app), (std::vector<std::int64_t>{33, 65}));
    EXPECT_EQ(app.Status().rfind("Generated 65 x 33", 0), 0u) << app.Status();
    for (const char* hidden : {"Stage", "Properties", "Settings", "Camera"}) {
        ASSERT_NE(app.Viewer().FindPanel(hidden), nullptr) << hidden;
        EXPECT_FALSE(app.Viewer().FindPanel(hidden)->IsVisible()) << hidden;
    }
    ASSERT_NE(app.Viewer().FindPanel("Terrain"), nullptr);
    ASSERT_NE(app.Viewer().FindPanel("Inspector"), nullptr);
    EXPECT_EQ(app.Editor().Selection(), "/layers/0") << "opening selects the first layer";
    EXPECT_FALSE(app.Viewer().GetViewportDisplay().grid);
    EXPECT_FALSE(app.Viewer().GetViewportDisplay().lights);
    const ernie::app::ViewportDisplay& display = app.Viewer().GetViewportDisplay();
    EXPECT_FALSE(display.selection);
    EXPECT_FALSE(display.toolbarRenderer || display.toolbarVar || display.toolbarCamera ||
                 display.toolbarLighting)
        << "the toolbar keeps only its settings and eye menus";
    EXPECT_TRUE(app.Viewer().FindPanel("Log")->IsVisible());

    app.SaveConfig(dir / "saved.json");
    EXPECT_EQ(app.Status(), "Saved " + (dir / "saved.json").string());
    EXPECT_EQ(terraingen::ConfigToJson(terraingen::LoadConfig(dir / "saved.json")),
              terraingen::ConfigToJson(terraingen::ParseConfig(kConfig)));
}

TEST(GuiApp, OpensAndSavesThroughTheFileDialog) {
    const std::filesystem::path dir = Directory("dialog");
    std::ofstream(dir / "terrain.json") << kConfig;
    GuiApp app({dir / "terrain.json", 1, true});
    Settle(app);
    ernie::app::FileDialog& dialog = app.Viewer().GetFileDialog();

    app.ShowSaveDialog();
    ASSERT_TRUE(dialog.IsOpen());
    EXPECT_TRUE(dialog.Options().save);
    EXPECT_EQ(dialog.Directory(), dir) << "starts beside the configuration";
    app.Step();
    EXPECT_TRUE(dialog.Accept("copy"));
    EXPECT_TRUE(std::filesystem::exists(dir / "copy.json"));
    EXPECT_EQ(app.Status(), "Saved " + (dir / "copy.json").string());

    app.ShowOpenDialog();
    ASSERT_TRUE(dialog.IsOpen());
    EXPECT_FALSE(dialog.Options().save);
    EXPECT_EQ(dialog.Name(), "copy.json") << "suggests the configuration in use";
    app.Step();
    EXPECT_TRUE(dialog.Accept("terrain.json"));
    Settle(app);
    EXPECT_EQ(app.Status().rfind("Generated", 0), 0u) << app.Status();
}

TEST(GuiApp, LiveEditsRegenerateAndBadJsonKeepsTheLastDocument) {
    const std::filesystem::path dir = Directory("live");
    std::ofstream(dir / "terrain.json") << kConfig;
    GuiApp app({dir / "terrain.json", 1, true});
    Settle(app);
    EXPECT_EQ(app.ConfigDocument()["outputs"][0]["extent_m"]["size"][0], 130);
    EXPECT_TRUE(app.ConfigParseError().empty());

    app.SetLive(true);
    EXPECT_TRUE(app.Live());
    app.EditConfig(
        [](nlohmann::ordered_json& config) { config["outputs"][0]["extent_m"]["size"][0] = 66; });
    EXPECT_NE(app.ConfigText().find("66,\n"), std::string::npos) << "the text follows";
    EXPECT_LT(app.ConfigText().find("\"outputs\""), app.ConfigText().find("\"layers\""))
        << "an edit keeps the file's key order";
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::minutes(1);
    while (HeightsShape(app) != std::vector<std::int64_t>{33, 33} &&
           std::chrono::steady_clock::now() < give_up) {
        app.Step();
    }
    EXPECT_EQ(HeightsShape(app), (std::vector<std::int64_t>{33, 33})) << app.Status();
    app.Editor().ExpandAll();
    app.Step();

    app.SetConfigText("{ \"outputs\": ");
    EXPECT_FALSE(app.ConfigParseError().empty());
    EXPECT_EQ(app.ConfigDocument()["outputs"][0]["extent_m"]["size"][0], 66)
        << "the last good document stays";
    app.SetLive(false);
    app.Step();
    EXPECT_FALSE(app.Generating());
}

TEST(GuiApp, ShowsGeoTiffHeightsAndStartsWithTheStarterConfig) {
    const std::filesystem::path dir = Directory("geotiff");
    terraingen::Raster<float> heights(20, 12, 1.0f);
    heights.SetTransform(terraingen::GeoTransform::NorthUp(0.0, 24.0, 2.0));
    terraingen::WriteGeoTiff(dir / "dem.tif", heights);
    GuiApp app({{}, 1, true});
    EXPECT_NO_THROW(terraingen::ParseConfig(app.ConfigText())) << "the starter config parses";
    EXPECT_TRUE(HeightsShape(app).empty());
    app.Open(dir / "dem.tif");
    EXPECT_EQ(HeightsShape(app), (std::vector<std::int64_t>{12, 20}));
    EXPECT_EQ(app.Status(), "Opened " + (dir / "dem.tif").string() + ", 20 x 12");
    app.Step();
}

TEST(GuiApp, ReportsErrorsWithoutStopping) {
    const std::filesystem::path dir = Directory("errors");
    GuiApp app({{}, 1, true});
    app.Open(dir / "missing.json");
    EXPECT_EQ(app.Status().rfind("Cannot open", 0), 0u) << app.Status();
    app.SetConfigText("{ not json");
    app.Generate();
    EXPECT_FALSE(app.Generating());
    EXPECT_EQ(app.Status().rfind("Invalid configuration", 0), 0u) << app.Status();
    app.SaveConfig(dir / "bad.json");
    EXPECT_EQ(app.Status().rfind("Cannot save", 0), 0u) << app.Status();
    EXPECT_FALSE(std::filesystem::exists(dir / "bad.json"));

    app.SetConfigText(R"({"schema_version": 1, "source": {"type": "raster", "path": "no.tif"}})");
    app.Generate();
    Settle(app);
    EXPECT_EQ(app.Status().rfind("Generation failed", 0), 0u) << app.Status();
    EXPECT_FALSE(app.Viewer().LogEntries().empty());
    EXPECT_TRUE(app.Step());
}
