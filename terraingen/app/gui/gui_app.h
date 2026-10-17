/// @file
/// @brief TerrainGen's GUI: a viewer of the terrain with panels to open, edit and generate it.
#pragma once

#include "terraingen/app/gui/config_editor.h"
#include "terraingen/gpu/backend.h"
#include "terraingen/raster/raster.h"

#include <ernie/app/viewer.h>
#include <ernie/scene/scene.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace terraingen::gui {

/// Options for opening a `GuiApp`.
struct GuiAppOptions {
    std::filesystem::path open;  ///< Configuration (.json) or heights (.tif, .tiff); may be empty.
    unsigned threads = 0;        ///< Generation threads; 0 for every hardware thread.
    /// Where generation evaluates heights; empty for `DefaultBackend()`.
    std::optional<Backend> backend = std::nullopt;
    bool hidden = false;               ///< Render without showing the window, for tests.
    std::string title = "TerrainGen";  ///< The window title.
};

/// TerrainGen's GUI (`terraingen --gui`): Ernie's viewer showing the terrain, with two
/// panels of its own.
///
/// - Terrain, on the left: open (Ctrl+O), save (Ctrl+S), generate (F5) and Live regeneration, the
///   file and status, and the configuration's outliner (`ConfigEditor`);
/// - Inspector, on the right: the settings of the outliner's selection, and the configuration as
///   JSON.
///
/// Of Ernie's own panels only the log starts visible, and the viewport's grid, light gizmos,
/// selection outline and most toolbar menus start off; the Window and eye menus bring them back.
/// Generation runs on a thread of its own; the finished terrain replaces the scene's on the next
/// frame, and the first one shown is framed.
class GuiApp {
  public:
    /// Opens the window, then `Open`s `options.open` when given.
    /// @throws std::runtime_error if the window or GPU cannot be initialized.
    explicit GuiApp(const GuiAppOptions& options = {});
    /// Waits for a running generation, then closes the window.
    ~GuiApp();
    GuiApp(const GuiApp&) = delete;
    GuiApp& operator=(const GuiApp&) = delete;

    /// Draws frames until the window closes.
    void Run();

    /// Draws one frame; returns whether the window is still open.
    bool Step();

    /// The viewer the GUI runs in.
    ernie::app::Viewer& Viewer();
    /// The scene the viewer shows.
    ernie::Scene& Scene();

    /// Opens a configuration, putting it in the editor and generating it, or heights,
    /// showing them at once. Errors go to the log and the status.
    void Open(const std::filesystem::path& path);

    /// Shows heights built elsewhere, such as in code, as the scene's terrain, and returns whether
    /// they were shown. Errors go to the log and the status.
    /// @param albedo The albedo on the heights' grid, or null.
    /// @param status The status line to report once shown.
    /// @param frame Whether to frame the terrain; the first terrain shown is always framed.
    bool ShowHeights(const Raster<float>& heights, const Raster<float>* albedo, bool geographic,
                     const std::string& status, bool frame = false);

    /// Adds a message to the viewer's log and makes it the status line.
    void Report(ernie::app::LogLevel level, std::string text);

    /// Generates the editor's configuration in the background; does nothing while a
    /// generation runs. Relative paths resolve against the folder of the configuration file last
    /// opened or saved, else the working directory.
    void Generate();

    /// Whether a generation is running.
    bool Generating() const;

    /// The configuration being edited, as JSON text (the Inspector's JSON tab).
    const std::string& ConfigText() const;
    /// Replaces the configuration's text and parses it.
    void SetConfigText(std::string json);

    /// The configuration as the outliner and inspector edit it: the last text that parsed.
    const nlohmann::ordered_json& ConfigDocument() const;

    /// Why `ConfigText()` does not parse as JSON; empty when it parses.
    const std::string& ConfigParseError() const;

    /// Edits the configuration as the outliner and inspector do: the text follows the
    /// document, and with `Live` a generation starts shortly after.
    void EditConfig(const std::function<void(nlohmann::ordered_json&)>& edit);

    /// The outliner's and inspector's editor.
    ConfigEditor& Editor();

    /// Whether each edit regenerates the terrain, a quarter second after the last one.
    bool Live() const;
    /// Turns Live regeneration on or off.
    void SetLive(bool live);

    /// Writes the editor's configuration, after checking it parses, and makes it the file
    /// `Generate` resolves paths against. Errors go to the log and the status.
    void SaveConfig(const std::filesystem::path& path);

    /// Opens the viewer's file dialog to choose a configuration or heights to `Open`,
    /// starting at the configuration file.
    void ShowOpenDialog();

    /// Opens the viewer's file dialog to choose where to `SaveConfig`.
    void ShowSaveDialog();

    /// The status line: what last happened, such as generated, opened, saved or failed.
    const std::string& Status() const;

  private:
    /// Draws the Terrain panel: the toolbar, the file and status, and the outliner.
    void DrawTerrainPanel();
    /// Draws the Inspector panel: the selection's settings, and the configuration as JSON.
    void DrawInspectorPanel();

    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace terraingen::gui
