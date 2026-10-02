#pragma once

#include <memory>

namespace terraingen::viewer {

/// Command-line options of `terraingen-viewer`.
struct AppOptions {
    long maxFrames = -1;  ///< Quit after this many frames; negative runs until the window closes.
};

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
