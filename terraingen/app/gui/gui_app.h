/// @file
/// @brief TerrainGen's GUI: a viewer of the terrain with panels to open, edit and generate it.
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

/// @brief Options for opening a `GuiApp`.
struct GuiAppOptions {
    std::filesystem::path open;  ///< Configuration (.json) or heights (.tif, .tiff); may be empty.
    unsigned threads = 0;        ///< Generation threads; 0 for every hardware thread.
    bool hidden = false;         ///< Render without showing the window, for tests.
    std::string title = "TerrainGen";  ///< The window title.
};

/// @brief TerrainGen's GUI (`terraingen --gui`): Ernie's viewer showing the terrain, with two
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
    /// @brief Opens the window, then `Open`s `options.open` when given.
    /// @param options The file to open, threads, visibility and title.
    /// @throws std::runtime_error if the window or GPU cannot be initialized.
    explicit GuiApp(const GuiAppOptions& options = {});
    /// @brief Waits for a running generation, then closes the window.
    ~GuiApp();
    GuiApp(const GuiApp&) = delete;
    GuiApp& operator=(const GuiApp&) = delete;

    /// @brief Draws frames until the window closes.
    void Run();

    /// @brief Draws one frame.
    /// @return Whether the window is still open.
    bool Step();

    /// @brief The viewer the GUI runs in.
    /// @return The viewer.
    ernie::app::Viewer& Viewer();
    /// @brief The scene the viewer shows.
    /// @return The scene.
    ernie::Scene& Scene();

    /// @brief Opens a configuration, putting it in the editor and generating it, or heights,
    /// showing them at once. Errors go to the log and the status.
    /// @param path A configuration (.json) or heights (.tif, .tiff).
    void Open(const std::filesystem::path& path);

    /// @brief Shows heights built elsewhere, such as in code, as the scene's terrain. Errors go to
    /// the log and the status.
    /// @param heights The heights.
    /// @param albedo The albedo on the heights' grid, or null.
    /// @param geographic Whether the heights are in longitude and latitude.
    /// @param status The status line to report once shown.
    /// @param frame Whether to frame the terrain; the first terrain shown is always framed.
    /// @return Whether the terrain was shown.
    bool ShowHeights(const Raster<float>& heights, const Raster<float>* albedo, bool geographic,
                     const std::string& status, bool frame = false);

    /// @brief Adds a message to the viewer's log and makes it the status line.
    /// @param level The log level.
    /// @param text The message.
    void Report(ernie::app::LogLevel level, std::string text);

    /// @brief Generates the editor's configuration in the background; does nothing while a
    /// generation runs. Relative paths resolve against the folder of the configuration file last
    /// opened or saved, else the working directory.
    void Generate();

    /// @brief Whether a generation is running.
    /// @return True while a generation runs.
    bool Generating() const;

    /// @brief The configuration being edited, as JSON text (the Inspector's JSON tab).
    /// @return The text.
    const std::string& ConfigText() const;
    /// @brief Replaces the configuration's text and parses it.
    /// @param json The new JSON text.
    void SetConfigText(std::string json);

    /// @brief The configuration as the outliner and inspector edit it: the last text that parsed.
    /// @return The parsed document.
    const nlohmann::json& ConfigDocument() const;

    /// @brief Why `ConfigText()` does not parse as JSON.
    /// @return The parse error; empty when the text parses.
    const std::string& ConfigParseError() const;

    /// @brief Edits the configuration as the outliner and inspector do: the text follows the
    /// document, and with `Live` a generation starts shortly after.
    /// @param edit Changes the document.
    void EditConfig(const std::function<void(nlohmann::json&)>& edit);

    /// @brief The outliner's and inspector's editor.
    /// @return The editor.
    ConfigEditor& Editor();

    /// @brief Whether each edit regenerates the terrain, a quarter second after the last one.
    /// @return True when Live is on.
    bool Live() const;
    /// @brief Turns Live regeneration on or off.
    /// @param live Whether edits regenerate the terrain.
    void SetLive(bool live);

    /// @brief Writes the editor's configuration, after checking it parses, and makes it the file
    /// `Generate` resolves paths against. Errors go to the log and the status.
    /// @param path The file to write.
    void SaveConfig(const std::filesystem::path& path);

    /// @brief Opens the viewer's file dialog to choose a configuration or heights to `Open`,
    /// starting at the configuration file.
    void ShowOpenDialog();

    /// @brief Opens the viewer's file dialog to choose where to `SaveConfig`.
    void ShowSaveDialog();

    /// @brief One line on what last happened: generated, opened, saved or failed.
    /// @return The status line.
    const std::string& Status() const;

  private:
    /// @brief Draws the Terrain panel: the toolbar, the file and status, and the outliner.
    void DrawTerrainPanel();
    /// @brief Draws the Inspector panel: the selection's settings, and the configuration as JSON.
    void DrawInspectorPanel();

    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace terraingen::gui
