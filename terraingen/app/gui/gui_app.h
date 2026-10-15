#pragma once

#include "terraingen/app/gui/config_editor.h"
#include "terraingen/raster/raster.h"

#include <ernie/app/viewer.h>
#include <ernie/scene/scene.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace terraingen::gui {

/// Options for opening a `GuiApp`.
struct GuiAppOptions {
    std::filesystem::path open;  ///< Configuration (.json) or heights (.tif, .tiff); may be empty.
    unsigned threads = 0;        ///< Generation threads; 0 for every hardware thread.
    bool hidden = false;         ///< Render without showing the window, for tests.
    std::string title = "TerrainGen";  ///< The window title.
};

/// TerrainGen's GUI (`terraingen --gui`): Ernie's viewer (`ernie::app::Viewer`) on a scene of the
/// terrain, with
/// Ernie's own panels hidden (the Window menu shows them again) but for its log, the viewport's
/// ground grid, light gizmos, selection outline and toolbar menus but its settings and eye off
/// (the eye menu shows them again), and two panels of its own:
/// - Terrain, on the left: a toolbar to open (Ctrl+O), save (Ctrl+S) and generate (F5) with Live
///   regeneration, the file and status, and the configuration's outliner (`ConfigEditor`);
/// - Inspector, on the right: the settings of what the outliner selects, and the whole
///   configuration as JSON.
/// Opening and saving go through the viewer's file dialogs (also under the File menu).
///
/// Generation runs on a thread of its own, so the GUI stays responsive; the finished terrain
/// replaces the scene's on the next frame, and the first one is framed.
class GuiApp {
  public:
    /// Opens the window, then `Open`s `options.open` when given.
    /// @throws std::runtime_error if the window or GPU cannot be initialized.
    explicit GuiApp(const GuiAppOptions& options = {});
    ~GuiApp();
    GuiApp(const GuiApp&) = delete;
    GuiApp& operator=(const GuiApp&) = delete;

    /// Draws frames until the window closes.
    void Run();

    /// Draws one frame; returns whether the window is still open.
    bool Step();

    ernie::app::Viewer& Viewer();
    ernie::Scene& Scene();

    /// Opens a configuration (.json), putting it in the editor and generating it, or heights
    /// (.tif, .tiff), showing them at once. Errors go to the viewer's log and the panel's status.
    void Open(const std::filesystem::path& path);

    /// Shows `heights`, with `albedo` when given (on the same grid), as the scene's terrain,
    /// reporting `status`, and frames them when `frame` or when nothing was shown before, as for
    /// terrains built in code (the examples' browser). Errors go to the log and the status.
    /// @return whether the terrain was shown.
    bool ShowHeights(const Raster<float>& heights, const Raster<float>* albedo, bool geographic,
                     const std::string& status, bool frame = false);

    /// Adds `text` to the viewer's log and makes it the status line.
    void Report(ernie::app::LogLevel level, std::string text);

    /// Generates the editor's configuration in the background; relative paths in it resolve
    /// against the folder of the configuration file last opened or saved, else the working
    /// directory. Does nothing while a generation runs.
    void Generate();

    /// Whether a generation is running.
    bool Generating() const;

    /// The configuration being edited, as JSON text (the Inspector's JSON tab).
    const std::string& ConfigText() const;
    void SetConfigText(std::string json);

    /// The configuration parsed, as the outliner and inspector edit it: the last text that
    /// parsed.
    const nlohmann::json& ConfigDocument() const;

    /// Why `ConfigText()` does not parse as JSON; empty when it does.
    const std::string& ConfigParseError() const;

    /// Edits the configuration as the outliner and inspector do: `edit` changes the document,
    /// the text follows, and with `Live` a generation starts shortly after.
    void EditConfig(const std::function<void(nlohmann::json&)>& edit);

    /// The outliner's and inspector's editor.
    ConfigEditor& Editor();

    /// Whether each edit regenerates the terrain, a quarter second after the last one.
    bool Live() const;
    void SetLive(bool live);

    /// Writes the editor's configuration to `path`, after checking it parses, and makes it the
    /// file `Generate` resolves paths against. Errors go to the log and the status.
    void SaveConfig(const std::filesystem::path& path);

    /// Opens the viewer's file dialog to choose a configuration or heights to `Open`, starting in
    /// the folder of the configuration file, as the panel's Open button and File > Open do.
    void ShowOpenDialog();

    /// Opens the viewer's file dialog to choose where to `SaveConfig`, as Save As does.
    void ShowSaveDialog();

    /// One line on what last happened: generated, opened, saved or failed.
    const std::string& Status() const;

  private:
    /// The Terrain panel: the toolbar, the file and status, and the outliner.
    void DrawTerrainPanel();
    /// The Inspector panel: the selection's settings, and the configuration as JSON.
    void DrawInspectorPanel();

    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace terraingen::gui
