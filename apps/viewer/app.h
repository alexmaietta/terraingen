#pragma once

#include "terraingen/core/log.h"

#include <memory>
#include <string>

namespace terraingen::viewer {

/// Command-line options of `terraingen-viewer`.
struct AppOptions {
    long maxFrames = -1;       ///< Quit after this many frames; negative runs until closed.
    std::string brdf;          ///< Reflectance model id to start with; empty keeps the default.
    std::string detailAlbedo;  ///< Detail albedo texture to load at startup.
    std::string detailNormal;  ///< Detail normal map to load at startup.
    LogLevel logLevel = LogLevel::Info;  ///< Lowest level shown in the Log panel.
};

/// Usage text for `terraingen-viewer --help`.
std::string AppUsage();

/// Parses `terraingen-viewer` arguments (`argv[0]` is the program name).
/// @throws std::invalid_argument for an unknown flag, a flag without its value, or a frame count
/// that is not an integer.
AppOptions ParseAppOptions(int argc, const char* const* argv);

/// The TerrainGen viewer: a window that generates synthetic terrain from its panels and renders it.
class App {
  public:
    /// Opens the window and initializes WebGPU and ImGui.
    /// @throws std::runtime_error if the window or GPU cannot be initialized.
    explicit App(const AppOptions& options);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    /// Runs the event loop until the window closes or `maxFrames` is reached.
    void Run();

  private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace terraingen::viewer
