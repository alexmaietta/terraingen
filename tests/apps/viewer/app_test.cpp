#include "app.h"
#include "settings.h"

#include "ernie/gpu/vulkan_context.h"
#include "ernie/gui/gui.h"
#include "ernie/scene/tokens.h"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

namespace tokens = ernie::tokens;
using terraingen::viewer::App;
using terraingen::viewer::AppOptions;

/// A hidden app with validation; skips without a Vulkan device.
std::unique_ptr<App> MakeApp(AppOptions options = {}) {
    try {
        ernie::VulkanContext::Create();
    } catch (const std::runtime_error& error) {
        return nullptr;
    }
    options.hidden = true;
    options.validation = true;
    options.logLevel = terraingen::LogLevel::Off;
    return std::make_unique<App>(options);
}

/// Steps until the terrain prim has heights, for at most `seconds`.
bool StepUntilTerrain(App& app, double seconds) {
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::duration<double>(seconds)) {
        app.Viewer().Step();
        if (app.Viewer().GetScene().Read(terraingen::viewer::kTerrainPrim, tokens::kHeights) !=
            nullptr) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(App, RunsInTheErnieViewerWithTerrainGensPanels) {
    std::unique_ptr<App> app = MakeApp();
    if (!app) {
        GTEST_SKIP() << "No Vulkan device";
    }
    ernie::gui::Viewer& viewer = app->Viewer();
    for (const char* title : {"Terrain", "View", "Surface", "Info", "Log", "Stats"}) {
        EXPECT_NE(viewer.FindPanel(title), nullptr) << title;
    }
    EXPECT_EQ(viewer.FindPanel("Terrain")->GetDock(), ernie::gui::Dock::Left);
    EXPECT_EQ(viewer.FindPanel("Log")->GetDock(), ernie::gui::Dock::Bottom);
    EXPECT_EQ(viewer.GetLighting(), ernie::gui::Lighting::Stage) << "TerrainGen's sun lights it";
    EXPECT_EQ(viewer.ActiveCamera(), ernie::gui::Viewer::PerspectiveCamera());

    ASSERT_TRUE(StepUntilTerrain(*app, 60.0)) << "the worker's first terrain reaches the scene";
    const ernie::Scene& scene = viewer.GetScene();
    EXPECT_TRUE(scene.Has(ernie::Path(terraingen::viewer::kSunPrim)));
    EXPECT_NE(scene.Get(viewer.ActiveCamera(), tokens::kExposureIso), nullptr)
        << "the View panel's exposure goes to the camera the viewer looks through";
    EXPECT_GT(viewer.Stats().triangles, 0u);

    for (const std::string& message : viewer.Context().TakeValidationMessages()) {
        ADD_FAILURE() << "Vulkan validation: " << message;
    }
}

TEST(App, RunStopsAfterTheFrameLimit) {
    AppOptions options;
    options.maxFrames = 3;
    std::unique_ptr<App> app = MakeApp(options);
    if (!app) {
        GTEST_SKIP() << "No Vulkan device";
    }
    int frames = 0;
    app->Viewer().OnFrame([&](ernie::gui::Viewer&, const ernie::gui::FrameInfo&) { ++frames; });
    app->Run();
    EXPECT_EQ(frames, 3);
    EXPECT_TRUE(app->Viewer().IsOpen());
}

TEST(App, RejectsAnUnknownReflectanceModel) {
    AppOptions options;
    options.brdf = "no_such_model";
    try {
        ernie::VulkanContext::Create();
    } catch (const std::runtime_error&) {
        GTEST_SKIP() << "No Vulkan device";
    }
    options.hidden = true;
    options.logLevel = terraingen::LogLevel::Off;
    try {
        App app(options);
        FAIL() << "an unknown model was accepted";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("available: lambert"), std::string::npos);
    }
}
