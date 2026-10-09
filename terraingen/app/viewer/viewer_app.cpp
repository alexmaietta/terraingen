#include "terraingen/app/viewer/viewer_app.h"

#include "terraingen/app/viewer/terrain_scene.h"
#include "terraingen/pipeline/config_json.h"
#include "terraingen/pipeline/run.h"
#include "terraingen/raster/geotiff.h"

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

namespace terraingen::viewer {

namespace {

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

/// What a generation hands back to the viewer's thread.
struct Generated {
    GeneratedTerrain terrain;
    double seconds = 0.0;
};

}  // namespace

struct ViewerApp::State {
    ernie::Scene scene;
    std::optional<ernie::app::Viewer> viewer;
    unsigned threads = 0;
    std::string config = kStarterConfig;
    std::filesystem::path configFile;  ///< The file the editor's configuration came from.
    std::string status = "Open a configuration or heights, or generate the starter terrain";
    std::future<Generated> generation;
    bool framed = false;

    void Report(ernie::app::LogLevel level, std::string text) {
        status = text;
        viewer->Log(level, std::move(text));
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
            text << "Generated " << t.heights.Width() << " x " << t.heights.Height() << " with "
                 << t.features.craters.Size() << " craters and " << t.features.rocks.size()
                 << " rocks in " << done.seconds << " s";
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

ViewerApp::ViewerApp(const ViewerAppOptions& options) : state_(std::make_unique<State>()) {
    State& s = *state_;
    s.threads = options.threads;
    SetUpScene(s.scene);
    ernie::app::ViewerOptions viewerOptions;
    viewerOptions.title = "TerrainGen";
    viewerOptions.hidden = options.hidden;
    s.viewer.emplace(s.scene, viewerOptions);
    ernie::app::ViewportDisplay display = s.viewer->GetViewportDisplay();
    display.grid = false;
    display.lights = false;
    s.viewer->SetViewportDisplay(display);
    for (const char* title : kHiddenPanels) {
        if (ernie::app::Panel* panel = s.viewer->FindPanel(title)) {
            panel->SetVisible(false);
        }
    }
    s.viewer->FocusPanel("Log");
    s.viewer->OnFrame(
        [this](ernie::app::Viewer&, const ernie::app::FrameInfo&) { state_->Collect(); });
    s.viewer->AddPanel("TerrainGen", ernie::app::Dock::Right, [this](ernie::app::Viewer&) {
        State& st = *state_;
        if (ImGui::Button("Open...")) {
            ShowOpenDialog();
        }
        ImGui::SameLine();
        if (ImGui::Button("Save")) {
            if (st.configFile.empty()) {
                ShowSaveDialog();
            } else {
                SaveConfig(st.configFile);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save As...")) {
            ShowSaveDialog();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(Generating());
        if (ImGui::Button(Generating() ? "Generating..." : "Generate")) {
            Generate();
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("%s", st.configFile.empty() ? "Unsaved configuration"
                                                        : st.configFile.string().c_str());
        ImGui::TextWrapped("%s", st.status.c_str());
        ImGui::Separator();
        const ImVec2 size(-1.0f, -1.0f);
        InputString("##config", st.config, &size);
    });
    s.viewer->AddMenuItem("File", "Open...", [this](ernie::app::Viewer&) { ShowOpenDialog(); });
    s.viewer->AddMenuItem("File", "Save Configuration As...",
                          [this](ernie::app::Viewer&) { ShowSaveDialog(); });
    if (!options.open.empty()) {
        Open(options.open);
    }
}

ViewerApp::~ViewerApp() {
    if (state_->generation.valid()) {
        state_->generation.wait();
    }
}

void ViewerApp::Run() {
    state_->viewer->Run();
}

bool ViewerApp::Step() {
    return state_->viewer->Step();
}

ernie::app::Viewer& ViewerApp::Viewer() {
    return *state_->viewer;
}

ernie::Scene& ViewerApp::Scene() {
    return state_->scene;
}

void ViewerApp::Open(const std::filesystem::path& path) {
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
        s.configFile = path;
        s.Report(ernie::app::LogLevel::Info, "Opened " + path.string());
        Generate();
    } catch (const std::exception& error) {
        s.Report(ernie::app::LogLevel::Error, "Cannot open " + path.string() + ": " + error.what());
    }
}

void ViewerApp::Generate() {
    State& s = *state_;
    if (Generating()) {
        return;
    }
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

bool ViewerApp::Generating() const {
    const std::future<Generated>& g = state_->generation;
    return g.valid() && g.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

const std::string& ViewerApp::ConfigText() const {
    return state_->config;
}

void ViewerApp::SetConfigText(std::string json) {
    state_->config = std::move(json);
}

void ViewerApp::SaveConfig(const std::filesystem::path& path) {
    State& s = *state_;
    try {
        const TerrainConfig config = ParseConfig(s.config);
        std::ofstream file(path);
        file << ConfigToJson(config, 4) << "\n";
        if (!file) {
            throw std::runtime_error("cannot write " + path.string());
        }
        s.configFile = path;
        s.Report(ernie::app::LogLevel::Info, "Saved " + path.string());
    } catch (const std::exception& error) {
        s.Report(ernie::app::LogLevel::Error, "Cannot save " + path.string() + ": " + error.what());
    }
}

void ViewerApp::ShowOpenDialog() {
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

void ViewerApp::ShowSaveDialog() {
    ernie::app::FileDialogOptions options;
    options.title = "Save Configuration";
    options.save = true;
    options.filters = {{"Configurations", {".json"}}};
    options.start = state_->configFile;
    state_->viewer->OpenFileDialog(
        std::move(options),
        [this](ernie::app::Viewer&, const std::filesystem::path& path) { SaveConfig(path); });
}

const std::string& ViewerApp::Status() const {
    return state_->status;
}

}  // namespace terraingen::viewer
