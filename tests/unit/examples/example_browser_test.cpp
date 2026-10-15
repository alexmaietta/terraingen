#include "common/browser.h"
#include "common/example.h"
#include "terraingen/app/gui/gui_app.h"
#include "terraingen/app/gui/terrain_scene.h"

#include <ernie/scene/tokens.h>
#include <ernie/util/tensor.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace ex = terraingen::examples;
namespace tokens = ernie::tokens;
using terraingen::gui::GuiApp;
using terraingen::gui::kMaterialPrim;
using terraingen::gui::kTerrainPrim;

/// A ramp `Width` cells wide shown twice, the second time with an albedo map; its check fails
/// when the first positional argument is "fail".
template <std::size_t Width> class Ramp : public ex::Example {
  public:
    using ex::Example::Example;

    void Generate() override {
        terraingen::Raster<float> heights(Width, 9);
        heights.SetTransform(terraingen::GeoTransform::NorthUp(0.0, 9.0, 1.0));
        for (std::size_t row = 0; row < 9; ++row) {
            for (std::size_t col = 0; col < Width; ++col) {
                heights.At(col, row) = static_cast<float>(col);
            }
        }
        Out() << "first line\nsecond line\n";
        Show("plain", heights);
        Show("with albedo", heights, &heights);
    }

    void Save(const std::filesystem::path& dir) override {
        std::ofstream(dir / "ramp.txt") << "ramp\n";
    }

    void Test() override {
        Check(Args().positional.empty() || Args().positional.front() != "fail", "not told to fail");
    }
};

class Broken : public ex::Example {
  public:
    using ex::Example::Example;
    void Generate() override {
        throw std::runtime_error("broken on purpose");
    }
    void Test() override {}
};

class Blank : public ex::Example {
  public:
    using ex::Example::Example;
    void Generate() override {
        Out() << "a table\n";
    }
    void Test() override {}
};

template <typename T> bool Register(const std::string& file) {
    return ex::RegisterExample(
        ex::MakeInfo(file, "About " + file, [](const ex::ExampleArgs& args, std::ostream& out) {
            return std::unique_ptr<ex::Example>(std::make_unique<T>(args, out));
        }));
}

const bool kRegistered = Register<Ramp<17>>("example_terrain_ramp.cpp") &&
                         Register<Ramp<33>>("example_craters_wide.cpp") &&
                         Register<Broken>("example_craters_broken.cpp") &&
                         Register<Blank>("example_distributions_blank.cpp");

/// Steps `app` until `browser` is idle, for at most a minute.
void Settle(GuiApp& app, ex::ExampleBrowser& browser) {
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::minutes(1);
    while (browser.Busy() && std::chrono::steady_clock::now() < give_up) {
        app.Step();
    }
    app.Step();
}

std::vector<std::int64_t> HeightsShape(GuiApp& app) {
    const ernie::Tensor* heights = app.Scene().Read(kTerrainPrim, tokens::kHeights);
    return heights != nullptr ? heights->Shape() : std::vector<std::int64_t>{};
}

bool LogHas(GuiApp& app, const std::string& text) {
    for (const ernie::app::LogEntry& entry : app.Viewer().LogEntries()) {
        if (entry.text.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(ExampleBrowser, GeneratesAndShowsThePickedExample) {
    ASSERT_TRUE(kRegistered);
    terraingen::gui::GuiAppOptions options;
    options.hidden = true;
    GuiApp app(options);
    ex::ExampleBrowser browser(app, {});
    EXPECT_EQ(browser.Current(), nullptr);
    EXPECT_EQ(browser.Status(), "Pick an example");
    for (const char* hidden : {"Terrain", "Inspector"}) {
        ASSERT_NE(app.Viewer().FindPanel(hidden), nullptr) << hidden;
        EXPECT_FALSE(app.Viewer().FindPanel(hidden)->IsVisible()) << hidden;
    }
    ASSERT_NE(app.Viewer().FindPanel("Examples"), nullptr);

    browser.Select(*ex::FindExample("terrain_ramp"));
    EXPECT_TRUE(browser.Busy());
    EXPECT_EQ(browser.CurrentExample(), nullptr);
    Settle(app, browser);
    ASSERT_NE(browser.CurrentExample(), nullptr);
    EXPECT_EQ(browser.Current(), ex::FindExample("terrain_ramp"));
    EXPECT_EQ(HeightsShape(app), (std::vector<std::int64_t>{9, 17}));
    EXPECT_EQ(browser.ViewIndex(), 0u);
    EXPECT_EQ(browser.Status().rfind("terrain_ramp: plain, 17 x 9", 0), 0u) << browser.Status();
    EXPECT_TRUE(LogHas(app, "second line"));
    EXPECT_FALSE(app.Scene().Has(kMaterialPrim) &&
                 app.Scene().Read(kMaterialPrim, tokens::kAlbedoMap) != nullptr);

    browser.ShowView(1);
    EXPECT_EQ(browser.ViewIndex(), 1u);
    EXPECT_NE(app.Scene().Read(kMaterialPrim, tokens::kAlbedoMap), nullptr);
    browser.ShowView(5);
    EXPECT_EQ(browser.ViewIndex(), 1u);

    browser.Select(*ex::FindExample("craters_wide"));
    Settle(app, browser);
    EXPECT_EQ(HeightsShape(app), (std::vector<std::int64_t>{9, 33}));
}

TEST(ExampleBrowser, ChecksAndSavesTheCurrentExample) {
    terraingen::gui::GuiAppOptions options;
    options.hidden = true;
    GuiApp app(options);
    const std::filesystem::path dir =
        std::filesystem::path(::testing::TempDir()) / "terraingen_example_browser";
    std::filesystem::remove_all(dir);
    ex::ExampleArgs args;
    args.outputDir = dir;
    ex::ExampleBrowser browser(app, args);
    EXPECT_FALSE(browser.RunTest());
    EXPECT_FALSE(browser.SaveOutputs());

    browser.Select(*ex::FindExample("terrain_ramp"));
    Settle(app, browser);
    EXPECT_TRUE(browser.RunTest());
    EXPECT_EQ(browser.Status(), "terrain_ramp: checks passed");
    EXPECT_TRUE(browser.SaveOutputs());
    EXPECT_TRUE(std::filesystem::exists(dir / "ramp.txt"));
    EXPECT_TRUE(LogHas(app, "Saved terrain_ramp to"));

    args.positional = {"fail"};
    ex::ExampleBrowser failing(app, args);
    failing.Select(*ex::FindExample("terrain_ramp"));
    Settle(app, failing);
    EXPECT_FALSE(failing.RunTest());
    EXPECT_EQ(failing.Status(), "terrain_ramp: Check failed: not told to fail");
}

TEST(ExampleBrowser, ReportsFailuresAndExamplesWithoutTerrain) {
    terraingen::gui::GuiAppOptions options;
    options.hidden = true;
    GuiApp app(options);
    ex::ExampleBrowser browser(app, {});
    browser.Select(*ex::FindExample("craters_broken"));
    Settle(app, browser);
    EXPECT_EQ(browser.CurrentExample(), nullptr);
    EXPECT_EQ(browser.Status(), "craters_broken failed: broken on purpose");

    browser.Select(*ex::FindExample("distributions_blank"));
    Settle(app, browser);
    ASSERT_NE(browser.CurrentExample(), nullptr);
    EXPECT_NE(browser.Status().find("no terrain to show"), std::string::npos);
    EXPECT_TRUE(LogHas(app, "a table"));
}

TEST(ExampleBrowser, RunsAPickMadeWhileGeneratingAfterwards) {
    terraingen::gui::GuiAppOptions options;
    options.hidden = true;
    GuiApp app(options);
    ex::ExampleBrowser browser(app, {});
    browser.Select(*ex::FindExample("terrain_ramp"));
    browser.Select(*ex::FindExample("craters_wide"));
    EXPECT_EQ(browser.Status(), "Waiting for terrain_ramp before craters_wide");
    Settle(app, browser);
    EXPECT_EQ(browser.Current(), ex::FindExample("craters_wide"));
    EXPECT_EQ(HeightsShape(app), (std::vector<std::int64_t>{9, 33}));
}

TEST(ExampleBrowser, RunWindowRendersHiddenAndChecks) {
    ex::ExampleArgs args;
    args.test = true;
    EXPECT_EQ(ex::RunWindow(nullptr, args, true), ex::kExitSuccess);
    EXPECT_EQ(ex::RunWindow(ex::FindExample("craters_broken"), args, true), ex::kExitFailure);
    args.positional = {"fail"};
    EXPECT_EQ(ex::RunWindow(ex::FindExample("craters_wide"), args, true), ex::kExitFailure);
}
