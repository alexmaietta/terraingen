#include "terraingen/app/viewer/terrain_scene.h"
#include "terraingen/app/viewer/viewer_app.h"
#include "terraingen/pipeline/config_json.h"
#include "terraingen/raster/geotiff.h"

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
using terraingen::viewer::kTerrainPrim;
using terraingen::viewer::ViewerApp;
using terraingen::viewer::ViewerAppOptions;

const char* const kConfig = R"({
    "schema_version": 1,
    "source": {"type": "flat", "width": 65, "height": 33, "pixel_size": 2},
    "layers": [{"type": "noise", "stack": [{"wavelength": 40, "amplitude": 3, "octaves": 3}]}]
})";

std::filesystem::path Directory(const std::string& name) {
    const std::filesystem::path directory =
        std::filesystem::path(::testing::TempDir()) / ("terraingen_viewer_" + name);
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
}

/// Steps `app` until no generation runs, for at most a minute.
void Settle(ViewerApp& app) {
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::minutes(1);
    while (app.Generating() && std::chrono::steady_clock::now() < give_up) {
        app.Step();
    }
    app.Step();
}

std::vector<std::int64_t> HeightsShape(ViewerApp& app) {
    const ernie::Tensor* heights = app.Scene().Read(kTerrainPrim, tokens::kHeights);
    return heights != nullptr ? heights->Shape() : std::vector<std::int64_t>{};
}

}  // namespace

TEST(ViewerApp, OpensAndGeneratesAConfigThenSavesIt) {
    const std::filesystem::path dir = Directory("generate");
    std::ofstream(dir / "terrain.json") << kConfig;
    ViewerApp app({dir / "terrain.json", 2, true});
    Settle(app);
    EXPECT_EQ(HeightsShape(app), (std::vector<std::int64_t>{33, 65}));
    EXPECT_EQ(app.Status().rfind("Generated 65 x 33", 0), 0u) << app.Status();
    for (const char* hidden : {"Stage", "Properties", "Settings", "Camera"}) {
        ASSERT_NE(app.Viewer().FindPanel(hidden), nullptr) << hidden;
        EXPECT_FALSE(app.Viewer().FindPanel(hidden)->IsVisible()) << hidden;
    }
    ASSERT_NE(app.Viewer().FindPanel("TerrainGen"), nullptr);
    EXPECT_FALSE(app.Viewer().GetViewportDisplay().grid);
    EXPECT_FALSE(app.Viewer().GetViewportDisplay().lights);
    EXPECT_TRUE(app.Viewer().FindPanel("Log")->IsVisible());

    app.SaveConfig(dir / "saved.json");
    EXPECT_EQ(app.Status(), "Saved " + (dir / "saved.json").string());
    EXPECT_EQ(terraingen::ConfigToJson(terraingen::LoadConfig(dir / "saved.json")),
              terraingen::ConfigToJson(terraingen::ParseConfig(kConfig)));
}

TEST(ViewerApp, OpensAndSavesThroughTheFileDialog) {
    const std::filesystem::path dir = Directory("dialog");
    std::ofstream(dir / "terrain.json") << kConfig;
    ViewerApp app({dir / "terrain.json", 1, true});
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

TEST(ViewerApp, ShowsGeoTiffHeightsAndStartsWithTheStarterConfig) {
    const std::filesystem::path dir = Directory("geotiff");
    terraingen::Raster<float> heights(20, 12, 1.0f);
    heights.SetTransform(terraingen::GeoTransform::NorthUp(0.0, 24.0, 2.0));
    terraingen::WriteGeoTiff(dir / "dem.tif", heights);
    ViewerApp app({{}, 1, true});
    EXPECT_NO_THROW(terraingen::ParseConfig(app.ConfigText())) << "the starter config parses";
    EXPECT_TRUE(HeightsShape(app).empty());
    app.Open(dir / "dem.tif");
    EXPECT_EQ(HeightsShape(app), (std::vector<std::int64_t>{12, 20}));
    EXPECT_EQ(app.Status(), "Opened " + (dir / "dem.tif").string() + ", 20 x 12");
    app.Step();
}

TEST(ViewerApp, ReportsErrorsWithoutStopping) {
    const std::filesystem::path dir = Directory("errors");
    ViewerApp app({{}, 1, true});
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
