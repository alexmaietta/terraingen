#pragma once

#include "ernie/gui/panel.h"
#include "ernie/gui/theme.h"
#include "ernie/scene/path.h"
#include "ernie/scene/scene.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ernie {
class RenderDelegate;
class VulkanContext;
}  // namespace ernie

namespace ernie::gui {

/// How the viewport lights the scene. The viewer opens with `Stage` when the scene has lights and
/// `Default` otherwise.
enum class Lighting {
    Stage,    ///< The scene's own lights, as a headless render draws them; black without any.
    Camera,   ///< A distant light along the view direction, ignoring the scene's lights.
    Default,  ///< A white sun from above with shadows, ignoring the scene's lights.
};

/// Options for opening a `Viewer`.
struct ViewerOptions {
    std::string title = "Ernie";   ///< Window title.
    std::uint32_t width = 1600;    ///< Window width in screen coordinates.
    std::uint32_t height = 900;    ///< Window height in screen coordinates.
    bool hidden = false;           ///< Render without showing the window, for tests.
    bool validation = false;       ///< Enable the Vulkan validation layer when installed.
    std::string device;            ///< GPU name to prefer; empty picks the first discrete.
    Theme theme = DarkTheme();     ///< Colours and metrics of the UI.
    std::filesystem::path layout;  ///< Docking layout (ImGui .ini) to load; empty uses
                                   ///< the built-in default layout.
    Path settings;                 ///< RenderSettings prim the viewport starts with;
                                   ///< empty picks the first one, as `ResolveRenderPrims`.
};

/// Per-frame information passed to `OnFrame` callbacks.
struct FrameInfo {
    std::uint64_t frame = 0;  ///< Frames drawn before this one.
    double time = 0.0;        ///< Seconds since the viewer opened.
    double delta = 0.0;       ///< Seconds since the previous frame.
};

/// An interactive window onto an Ernie `Scene`: a docked viewport rendering the scene with
/// a `RenderDelegate` (the rasterizer), plus Stage, Property, Render Settings, Sensor, Stats and
/// Log panels.
///
/// The viewer keeps no scene state of its own: panels read the scene each frame and write edits
/// back through `Scene::Set`, so edits from code and from the GUI are one history, seen by both.
/// What only the GUI needs (its cameras, the viewport's render product, lighting presets) is
/// defined under `SessionRoot()`, which panels hide unless asked to show it.
///
/// The viewer defines its own perspective camera on opening, framing the scene
/// (`PerspectiveCamera()`, the default view). The viewport looks through it or any Camera prim of
/// the scene; navigating moves whichever camera it looks through.
///
/// Drive it in one of two ways, both on the thread that created it (GLFW needs the main thread on
/// macOS):
/// - `Run()` loops until the window closes, calling `OnFrame` callbacks before each frame;
/// - `Step()` draws one frame and returns, for callers that own the loop, such as a simulation.
///
/// Applications extend it with panels (`AddPanel`), menu items (`AddMenuItem`) and callbacks;
/// TerrainGen's viewer is one such application, adding its terrain configuration panels.
class Viewer {
  public:
    /// Opens the window and initializes Vulkan, ImGui and the render delegate. `scene` must
    /// outlive the viewer.
    /// @throws std::runtime_error if the window or GPU cannot be initialized.
    explicit Viewer(Scene& scene, const ViewerOptions& options = {});
    ~Viewer();
    Viewer(const Viewer&) = delete;
    Viewer& operator=(const Viewer&) = delete;

    /// Draws frames until the window closes or `Close` is called.
    void Run();

    /// Processes window events, syncs the scene, renders the viewport and the UI and presents one
    /// frame. Returns whether the window is still open.
    bool Step();

    /// Whether the window is open.
    bool IsOpen() const;

    /// Asks the window to close; `Run` returns and `Step` returns false after the current frame.
    void Close();

    /// Adds a callback run at the start of every frame, before the scene is synced, so its edits
    /// show in that frame. Returns an id for `RemoveCallback`.
    std::uint64_t OnFrame(std::function<void(Viewer&, const FrameInfo&)> callback);

    /// Adds a callback run when the selection changes, by click or by `SetSelection`.
    std::uint64_t OnSelection(std::function<void(Viewer&, std::span<const Path>)> callback);

    void RemoveCallback(std::uint64_t id);

    Scene& GetScene();
    RenderDelegate& Delegate();
    VulkanContext& Context();
    const VulkanContext& Context() const;

    /// Root of the prims the viewer defines for its own use, `/__session`.
    static const Path& SessionRoot();

    /// The viewer's perspective camera, `/__session/Perspective`, framing the scene's bounds when
    /// the viewer opens.
    static const Path& PerspectiveCamera();

    const std::vector<Path>& Selection() const;
    void SetSelection(std::vector<Path> paths);

    /// The cameras the viewport can look through: the viewer's own, then the scene's Camera prims
    /// by path.
    std::vector<Path> Cameras() const;

    /// The Camera prim the viewport looks through; `PerspectiveCamera()` until changed. If the
    /// scene's camera is removed, the viewport falls back to `PerspectiveCamera()`.
    const Path& ActiveCamera() const;

    /// Looks through the Camera prim at `camera`, the viewer's or the scene's.
    /// @throws std::invalid_argument if `camera` is not a Camera prim.
    void SetActiveCamera(Path camera);

    /// Frames `paths` (the whole scene when empty) with the active camera (the F key).
    void Frame(std::span<const Path> paths = {});

    /// Defines a Camera prim at `path` in the scene with the active camera's transform and lens,
    /// and returns it.
    /// @throws std::invalid_argument if `path` exists or is under `SessionRoot()`.
    Path CameraFromView(const Path& path);

    /// The render var the viewport shows, such as `LdrColor`, `Depth`, `Normal` or `PrimId`.
    const std::string& ViewVar() const;

    /// @throws std::invalid_argument for a name not in `RenderTargets::VarNames`.
    void SetViewVar(std::string name);

    Lighting GetLighting() const;
    void SetLighting(Lighting lighting);

    /// The RenderSettings prim the viewport renders with.
    const Path& Settings() const;

    /// @throws std::invalid_argument if `settings` is not a RenderSettings prim.
    void SetSettings(Path settings);

    /// Writes the next viewport frame's `var` (the shown var when empty) to `path`, as PNG for
    /// 8-bit vars and OpenEXR otherwise.
    void Capture(const std::filesystem::path& path, std::string var = {});

    /// Adds a dockable panel, shown under Window in the menu bar. Returns the panel.
    Panel& AddPanel(std::unique_ptr<Panel> panel);

    /// Adds a `WidgetPanel` titled `title`, for widgets added without ImGui (Python's panels).
    WidgetPanel& AddWidgetPanel(std::string title);

    /// Removes the panel titled `title`; built-in panels are only hidden.
    void RemovePanel(const std::string& title);

    /// Adds `label` to the menu bar's `menu` (created if needed), running `action` when chosen.
    void AddMenuItem(const std::string& menu, const std::string& label,
                     std::function<void(Viewer&)> action);

    void SaveLayout(const std::filesystem::path& path) const;

    /// @throws std::runtime_error if the file cannot be read.
    void LoadLayout(const std::filesystem::path& path);

    const Theme& GetTheme() const;
    void SetTheme(const Theme& theme);

  private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace ernie::gui
