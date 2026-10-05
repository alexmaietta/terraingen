#pragma once

#include "terraingen/core/log.h"

#include <memory>
#include <string>

namespace ernie::gui {
class Viewer;
}  // namespace ernie::gui

namespace terraingen::viewer {

/// Command-line options of `terraingen-viewer`.
struct AppOptions {
    long maxFrames = -1;       ///< Quit after this many frames; negative runs until closed.
    std::string brdf;          ///< Reflectance model id to start with; empty keeps the default.
    std::string detailAlbedo;  ///< Detail albedo texture to load at startup.
    std::string detailNormal;  ///< Detail normal map to load at startup.
    std::string dem;           ///< GeoTIFF to view instead of synthetic terrain, without craters.
    LogLevel logLevel = LogLevel::Info;  ///< Lowest level shown in the Log panel.
    bool validation = false;  ///< Enable the Vulkan validation layer when it is installed.
    bool hidden = false;      ///< Render without a window, for tests and headless machines.
};

/// Usage text for `terraingen-viewer --help`.
std::string AppUsage();

/// Parses `terraingen-viewer` arguments (`argv[0]` is the program name).
/// @throws std::invalid_argument for an unknown flag, a flag without its value, a frame count
/// that is not an integer, or more than one DEM.
AppOptions ParseAppOptions(int argc, const char* const* argv);

/// The TerrainGen viewer: Ernie's GUI viewer (`ernie::gui::Viewer`) with TerrainGen's panels,
/// generating synthetic terrain or loading a DEM on a worker thread and showing it in the viewer's
/// scene.
class App {
  public:
    /// Opens the viewer and adds the Terrain, View, Surface, Info and Log panels.
    /// @throws std::runtime_error if the window or GPU cannot be initialized, or for an unknown
    /// reflectance model or a detail texture that cannot be loaded.
    explicit App(const AppOptions& options);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    /// Runs the event loop until the window closes or `maxFrames` is reached.
    void Run();

    /// The Ernie viewer the app runs in.
    ernie::gui::Viewer& Viewer();

  private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace terraingen::viewer
