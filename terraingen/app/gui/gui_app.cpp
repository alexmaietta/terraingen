#include "terraingen/app/gui/gui_app.h"

#include "terraingen/app/gui/config_editor.h"
#include "terraingen/app/gui/terrain_scene.h"
#include "terraingen/config/generate.h"
#include "terraingen/config/json.h"
#include "terraingen/raster/geotiff/geotiff.h"

#include <ernie/app/icons.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <exception>
#include <fstream>
#include <future>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace terraingen::gui {

namespace {

/// How long Live waits after the last edit before generating, so typing does not regenerate on
/// every key.
constexpr std::chrono::milliseconds kLiveDelay{250};

/// The outliner's panel, docked left, and the inspector's, docked right.
constexpr const char* kTerrainPanel = "Terrain";
constexpr const char* kInspectorPanel = "Inspector";

/// Lucide glyph of the Generate button (play).
constexpr const char* kGlyphPlay = "\xee\x84\xbc";

/// Ernie's panels hidden at start; the Window menu shows them again.
constexpr const char* kHiddenPanels[] = {"Stage", "Properties", "Settings", "Camera", "Profiler"};

/// A configuration to start from when nothing is opened: rolling noise on a 1 km square.
constexpr const char* kStarterConfig = R"({
    "schema_version": 1,
    "source": {
        "type": "flat",
        "width": 513,
        "height": 513,
        "pixel_size": 2
    },
    "layers": [
        {
            "type": "noise",
            "stack": [
                {
                    "seed": 1,
                    "fractal": "fbm",
                    "wavelength": 400,
                    "amplitude": 25,
                    "octaves": 6
                }
            ]
        }
    ]
}
)";

int ResizeString(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* text = static_cast<std::string*>(data->UserData);
        text->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = text->data();
    }
    return 0;
}

/// An ImGui text field editing `text`, single or multiline (`size` set).
bool InputString(const char* label, std::string& text, const ImVec2* size = nullptr) {
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
    if (size != nullptr) {
        return ImGui::InputTextMultiline(label, text.data(), text.capacity() + 1, *size,
                                         flags | ImGuiInputTextFlags_AllowTabInput, ResizeString,
                                         &text);
    }
    return ImGui::InputText(label, text.data(), text.capacity() + 1, flags, ResizeString, &text);
}

bool HasExtension(const std::filesystem::path& path, std::initializer_list<const char*> exts) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::any_of(exts.begin(), exts.end(), [&](const char* e) { return ext == e; });
}

/// What a generation hands back to the GUI's thread.
struct Generated {
    GeneratedTerrain terrain;
    double seconds = 0.0;
};

}  // namespace

struct GuiApp::State {
    ernie::Scene scene;
    std::optional<ernie::app::Viewer> viewer;
    unsigned threads = 0;
    std::string config = kStarterConfig;  ///< The configuration as text, as the JSON tab shows.
    nlohmann::ordered_json document;      ///< `config` parsed, keys in file order.
    std::string parseError;               ///< Why `config` does not parse; empty when it does.
    ConfigEditor editor;
    std::filesystem::path configFile;  ///< The file the editor's configuration came from.
    std::string savedConfig;           ///< `config` as last opened or saved.
    std::string status = "Open a configuration or heights, or generate the starter terrain";
    std::future<Generated> generation;
    bool framed = false;
    bool live = false;                                            ///< Regenerate after each edit.
    std::optional<std::chrono::steady_clock::time_point> edited;  ///< The last edit not generated.

    void Report(ernie::app::LogLevel level, std::string text) {
        status = text;
        viewer->Log(level, std::move(text));
    }

    /// Parses `config` into `document`, keeping the last good document when it does not parse.
    void ParseText() {
        try {
            document = nlohmann::ordered_json::parse(config);
            parseError.clear();
        } catch (const nlohmann::json::exception& error) {
            parseError = error.what();
        }
    }

    void Edited() {
        edited = std::chrono::steady_clock::now();
    }

    /// Puts a finished generation into the scene, framing the first terrain shown.
    void Collect() {
        if (!generation.valid() ||
            generation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }
        try {
            const Generated done = generation.get();
            const GeneratedTerrain& t = done.terrain;
            ShowTerrain(scene, t.heights, t.geoKeys.IsGeographic(),
                        t.albedo.empty() ? nullptr : &t.albedo.front());
            std::ostringstream text;
            text.precision(3);
            text << "Generated " << t.heights.Width() << " x " << t.heights.Height() << " in "
                 << done.seconds << " s";
            Report(ernie::app::LogLevel::Info, text.str());
            Shown();
        } catch (const std::exception& error) {
            Report(ernie::app::LogLevel::Error, std::string("Generation failed: ") + error.what());
        }
    }

    void Shown() {
        if (!framed) {
            viewer->Frame();
            framed = true;
        }
    }
};

GuiApp::GuiApp(const GuiAppOptions& options) : state_(std::make_unique<State>()) {
    State& s = *state_;
    s.threads = options.threads;
    SetUpScene(s.scene);
    ernie::app::ViewerOptions viewerOptions;
    viewerOptions.title = options.title;
    viewerOptions.hidden = options.hidden;
    viewerOptions.docks.left = 0.2f;
    viewerOptions.docks.right = 0.3f;
    viewerOptions.docks.bottom = 0.14f;
    s.viewer.emplace(s.scene, viewerOptions);
    ernie::app::ViewportDisplay display = s.viewer->GetViewportDisplay();
    display.grid = false;
    display.lights = false;
    display.selection = false;
    display.toolbarRenderer = false;
    display.toolbarVar = false;
    display.toolbarCamera = false;
    display.toolbarLighting = false;
    s.viewer->SetViewportDisplay(display);
    for (const char* title : kHiddenPanels) {
        if (ernie::app::Panel* panel = s.viewer->FindPanel(title)) {
            panel->SetVisible(false);
        }
    }
    s.viewer->FocusPanel("Log");
    s.ParseText();
    s.viewer->OnFrame([this](ernie::app::Viewer&, const ernie::app::FrameInfo&) {
        State& st = *state_;
        st.Collect();
        if (st.live && st.edited && st.parseError.empty() && !Generating() &&
            std::chrono::steady_clock::now() - *st.edited > kLiveDelay) {
            Generate();
        }
    });
    s.viewer->AddPanel(kTerrainPanel, ernie::app::Dock::Left,
                       [this](ernie::app::Viewer&) { DrawTerrainPanel(); });
    s.viewer->AddPanel(kInspectorPanel, ernie::app::Dock::Right,
                       [this](ernie::app::Viewer&) { DrawInspectorPanel(); });
    s.viewer->AddMenuItem("File", "Open...", [this](ernie::app::Viewer&) { ShowOpenDialog(); });
    s.viewer->AddMenuItem("File", "Save Configuration As...",
                          [this](ernie::app::Viewer&) { ShowSaveDialog(); });
    if (!options.open.empty()) {
        Open(options.open);
    }
}

GuiApp::~GuiApp() {
    if (state_->generation.valid()) {
        state_->generation.wait();
    }
}

void GuiApp::Run() {
    state_->viewer->Run();
}

bool GuiApp::Step() {
    return state_->viewer->Step();
}

ernie::app::Viewer& GuiApp::Viewer() {
    return *state_->viewer;
}

ernie::Scene& GuiApp::Scene() {
    return state_->scene;
}

void GuiApp::Open(const std::filesystem::path& path) {
    State& s = *state_;
    try {
        if (HasExtension(path, {".tif", ".tiff"})) {
            const GeoTiff tiff = ReadGeoTiff(path);
            ShowTerrain(s.scene, tiff.raster, tiff.geoKeys.IsGeographic());
            s.Report(ernie::app::LogLevel::Info, "Opened " + path.string() + ", " +
                                                     std::to_string(tiff.raster.Width()) + " x " +
                                                     std::to_string(tiff.raster.Height()));
            s.Shown();
            return;
        }
        std::ifstream file(path);
        if (!file) {
            throw std::runtime_error("cannot read " + path.string());
        }
        std::ostringstream text;
        text << file.rdbuf();
        s.config = text.str();
        s.savedConfig = s.config;
        s.ParseText();
        s.editor.Select("/layers/0");
        s.configFile = path;
        s.Report(ernie::app::LogLevel::Info, "Opened " + path.string());
        Generate();
    } catch (const std::exception& error) {
        s.Report(ernie::app::LogLevel::Error, "Cannot open " + path.string() + ": " + error.what());
    }
}

bool GuiApp::ShowHeights(const Raster<float>& heights, const Raster<float>* albedo, bool geographic,
                         const std::string& status, bool frame) {
    State& s = *state_;
    try {
        ShowTerrain(s.scene, heights, geographic, albedo);
    } catch (const std::exception& error) {
        s.Report(ernie::app::LogLevel::Error, std::string("Cannot show it: ") + error.what());
        return false;
    }
    s.Report(ernie::app::LogLevel::Info, status);
    if (frame) {
        s.framed = false;
    }
    s.Shown();
    return true;
}

void GuiApp::Report(ernie::app::LogLevel level, std::string text) {
    state_->Report(level, std::move(text));
}

void GuiApp::Generate() {
    State& s = *state_;
    if (Generating()) {
        return;
    }
    s.edited.reset();
    try {
        const TerrainConfig config = ParseConfig(s.config);
        const std::filesystem::path directory = s.configFile.parent_path();
        const unsigned threads = s.threads;
        s.generation = std::async(std::launch::async, [config, directory, threads] {
            const auto start = std::chrono::steady_clock::now();
            Generated done{GenerateTerrain(config, directory, threads), 0.0};
            done.seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            return done;
        });
        s.status = "Generating...";
    } catch (const std::exception& error) {
        s.Report(ernie::app::LogLevel::Error,
                 std::string("Invalid configuration: ") + error.what());
    }
}

bool GuiApp::Generating() const {
    const std::future<Generated>& g = state_->generation;
    return g.valid() && g.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

const std::string& GuiApp::ConfigText() const {
    return state_->config;
}

void GuiApp::SetConfigText(std::string json) {
    state_->config = std::move(json);
    state_->ParseText();
}

const nlohmann::ordered_json& GuiApp::ConfigDocument() const {
    return state_->document;
}

const std::string& GuiApp::ConfigParseError() const {
    return state_->parseError;
}

ConfigEditor& GuiApp::Editor() {
    return state_->editor;
}

bool GuiApp::Live() const {
    return state_->live;
}

void GuiApp::SetLive(bool live) {
    state_->live = live;
}

void GuiApp::EditConfig(const std::function<void(nlohmann::ordered_json&)>& edit) {
    State& s = *state_;
    edit(s.document);
    s.config = s.document.dump(4) + "\n";
    s.parseError.clear();
    s.Edited();
}

void GuiApp::SaveConfig(const std::filesystem::path& path) {
    State& s = *state_;
    try {
        const TerrainConfig config = ParseConfig(s.config);
        std::ofstream file(path);
        file << ConfigToJson(config, 4) << "\n";
        if (!file) {
            throw std::runtime_error("cannot write " + path.string());
        }
        s.configFile = path;
        s.savedConfig = s.config;
        s.Report(ernie::app::LogLevel::Info, "Saved " + path.string());
    } catch (const std::exception& error) {
        s.Report(ernie::app::LogLevel::Error, "Cannot save " + path.string() + ": " + error.what());
    }
}

void GuiApp::ShowOpenDialog() {
    ernie::app::FileDialogOptions options;
    options.title = "Open Configuration or Heights";
    options.filters = {{"Configurations and heights", {".json", ".tif", ".tiff"}},
                       {"Configurations", {".json"}},
                       {"Heights", {".tif", ".tiff"}},
                       {"All files", {}}};
    options.start = state_->configFile;
    state_->viewer->OpenFileDialog(
        std::move(options),
        [this](ernie::app::Viewer&, const std::filesystem::path& path) { Open(path); });
}

void GuiApp::ShowSaveDialog() {
    ernie::app::FileDialogOptions options;
    options.title = "Save Configuration";
    options.save = true;
    options.filters = {{"Configurations", {".json"}}};
    options.start = state_->configFile;
    state_->viewer->OpenFileDialog(
        std::move(options),
        [this](ernie::app::Viewer&, const std::filesystem::path& path) { SaveConfig(path); });
}

void GuiApp::DrawTerrainPanel() {
    State& s = *state_;
    // The toolbar: file actions, then Generate in the accent colour, then Live.
    if (ImGui::Button(ernie::app::icons::kFolderOpen)) {
        ShowOpenDialog();
    }
    ImGui::SetItemTooltip("Open a configuration or heights (Ctrl+O)");
    ImGui::SameLine();
    if (ImGui::Button(ernie::app::icons::kSave)) {
        if (s.configFile.empty()) {
            ShowSaveDialog();
        } else {
            SaveConfig(s.configFile);
        }
    }
    ImGui::SetItemTooltip("Save the configuration (Ctrl+S)");
    ImGui::SameLine();
    if (ImGui::Button((std::string(ernie::app::icons::kSave) + "...").c_str())) {
        ShowSaveDialog();
    }
    ImGui::SetItemTooltip("Save the configuration as a new file");
    ImGui::SameLine();
    const ImVec4 accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent.x, accent.y, accent.z, 0.75f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent.x, accent.y, accent.z, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent);
    ImGui::BeginDisabled(Generating());
    const std::string generate =
        std::string(kGlyphPlay) + (Generating() ? "  Generating..." : "  Generate");
    if (ImGui::Button(generate.c_str(), ImVec2(-ImGui::GetFrameHeight() * 2.6f, 0.0f))) {
        Generate();
    }
    ImGui::EndDisabled();
    ImGui::PopStyleColor(3);
    ImGui::SetItemTooltip("Generate the terrain (F5)");
    ImGui::SameLine();
    ImGui::Checkbox("Live", &s.live);
    ImGui::SetItemTooltip("Generate again a moment after each edit");

    // The file, starred when edited since it was opened or saved, and what last happened.
    const bool dirty = s.config != s.savedConfig;
    const std::string file = s.configFile.empty() ? "Untitled" : s.configFile.filename().string();
    ImGui::TextUnformatted((file + (dirty ? " *" : "")).c_str());
    if (!s.configFile.empty()) {
        ImGui::SetItemTooltip("%s", s.configFile.string().c_str());
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", s.status.c_str());
    ImGui::PopStyleColor();
    if (!s.parseError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "The JSON does not parse");
        ImGui::SetItemTooltip("%s", s.parseError.c_str());
    }

    if (ImGui::BeginChild("##outliner")) {
        if (s.editor.DrawOutliner(s.document)) {
            s.config = s.document.dump(4) + "\n";
            s.parseError.clear();
            s.Edited();
        }
    }
    ImGui::EndChild();

    const ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        ShowOpenDialog();
    }
    if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (s.configFile.empty()) {
            ShowSaveDialog();
        } else {
            SaveConfig(s.configFile);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
        Generate();
    }
}

void GuiApp::DrawInspectorPanel() {
    State& s = *state_;
    if (!ImGui::BeginTabBar("##inspectorTabs")) {
        return;
    }
    if (ImGui::BeginTabItem("Settings")) {
        if (ImGui::BeginChild("##settings")) {
            if (s.editor.DrawInspector(s.document)) {
                s.config = s.document.dump(4) + "\n";
                s.parseError.clear();
                s.Edited();
            }
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("JSON")) {
        const ImVec2 size(-1.0f, -1.0f);
        if (InputString("##config", s.config, &size)) {
            s.ParseText();
            s.Edited();
        }
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

const std::string& GuiApp::Status() const {
    return state_->status;
}

}  // namespace terraingen::gui
