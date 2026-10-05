#include "app.h"

#include "terraingen/core/version.h"

#include "ernie/gui/gui.h"
#include "ernie/scene/tokens.h"
#include "panels.h"
#include "settings.h"
#include "worker.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace terraingen::viewer {

namespace {

/// Routes TerrainGen's log to `buffer` for its lifetime, then restores the default sink and level.
class LogInstallation {
  public:
    explicit LogInstallation(LogBuffer& buffer) {
        SetLogSink(buffer.Sink());
    }
    ~LogInstallation() {
        SetLogSink(nullptr);
        SetLogLevel(LogLevel::Warning);
    }
    LogInstallation(const LogInstallation&) = delete;
    LogInstallation& operator=(const LogInstallation&) = delete;
};

}  // namespace

struct App::State {
    LogBuffer logBuffer;
    LogInstallation logInstallation{logBuffer};
    AppOptions options;
    ernie::Scene scene;
    std::unique_ptr<ernie::gui::Viewer> viewer;

    TerrainSettings settings;
    TerrainSettings requested;
    ViewSettings view;
    Worker worker;
    bool autoRegenerate = true;
    bool framed = false;
    std::array<double, 4> framedBounds{};
    TerrainPanelStatus status;
    InfoPanelData info;
    SurfacePanelState surfacePanel;
    LogPanelState logPanel;
    std::vector<ernie::gui::Panel*> panels;
    ernie::gui::Panel* log = nullptr;

    const ernie::BrdfRegistry& Brdfs() const {
        return viewer->Brdfs();
    }

    /// Applies the command-line DEM, reflectance model and detail textures.
    /// @throws std::runtime_error for an unknown model or a texture that cannot be loaded.
    void ApplyOptions() {
        if (!options.dem.empty()) {
            settings.source = TerrainSource::Raster;
            settings.raster.path = options.dem;
            settings.craters.enabled = false;
        }
        if (!options.brdf.empty()) {
            if (Brdfs().Find(options.brdf) == nullptr) {
                std::string known;
                for (const ernie::BrdfModel& model : Brdfs().Models()) {
                    known += (known.empty() ? "" : ", ") + model.id;
                }
                throw std::runtime_error("Unknown reflectance model '" + options.brdf +
                                         "'; available: " + known);
            }
            view.material.surface.brdf = options.brdf;
        }
        for (const bool normalMap : {false, true}) {
            const std::string& path = normalMap ? options.detailNormal : options.detailAlbedo;
            if (path.empty()) {
                continue;
            }
            auto& buffer = normalMap ? surfacePanel.normalPath : surfacePanel.albedoPath;
            path.copy(buffer.data(), buffer.size() - 1);
            if (!LoadDetail(normalMap)) {
                throw std::runtime_error(
                    "Cannot load detail " + std::string(normalMap ? "normal map" : "albedo") + " " +
                    path + ": " +
                    (normalMap ? surfacePanel.normalStatus : surfacePanel.albedoStatus));
            }
        }
    }

    void Request() {
        requested = settings;
        worker.Request(settings);
    }

    void ApplyResult(GenerationResult result) {
        status.error = result.error;
        if (!result.error.empty()) {
            LogWarning("viewer", "Generation ", result.id, " failed: ", result.error);
            return;
        }
        const Stopwatch upload;
        const std::size_t cols = result.raster.Width();
        const std::size_t rows = result.raster.Height();
        const Bounds2d extent = TerrainGrid{cols, rows, result.raster.Transform()}.Bounds();
        try {
            AuthorTerrain(scene, std::make_shared<const Raster<float>>(std::move(result.raster)));
            viewer->Sync();
        } catch (const std::invalid_argument& error) {
            status.error = error.what();
            LogWarning("viewer", "Cannot display generation ", result.id, ": ", status.error);
            return;
        }
        info.width = cols;
        info.height = rows;
        LogInfo("viewer", "Generation ", result.id, " finished in ",
                FormatDuration(result.milliseconds), "; uploaded ", info.width, "x", info.height,
                " heightfield in ", FormatDuration(upload.Milliseconds()));
        info.stats = result.stats;
        info.generationMilliseconds = result.milliseconds;

        const std::array<double, 4> bounds{extent.minX, extent.minY, extent.maxX, extent.maxY};
        if (!framed || bounds != framedBounds) {
            viewer->Frame();
            framed = true;
            framedBounds = bounds;
        }
    }

    /// Loads the detail texture at the path in the Surface panel; on failure records why there and
    /// returns false.
    bool LoadDetail(bool normalMap) {
        const std::string path =
            normalMap ? surfacePanel.normalPath.data() : surfacePanel.albedoPath.data();
        std::string& message = normalMap ? surfacePanel.normalStatus : surfacePanel.albedoStatus;
        auto loaded = LoadDetailTexture(path, normalMap);
        const char* kind = normalMap ? "normal map" : "albedo";
        if (const std::string* error = std::get_if<std::string>(&loaded)) {
            message = *error;
            LogWarning("viewer", "Cannot load detail ", kind, " ", path, ": ", message);
            return false;
        }
        auto image =
            std::make_shared<const ernie::Image>(std::move(std::get<ernie::Image>(loaded)));
        const std::string_view attribute =
            normalMap ? ernie::tokens::kDetailNormal : ernie::tokens::kDetailAlbedo;
        try {
            scene.Define(kSurfacePrim, ernie::tokens::kMaterial);
            scene.Define(kTerrainPrim, ernie::tokens::kTerrain)
                .Set(ernie::tokens::kMaterialBinding, ernie::Path(kSurfacePrim));
            scene.Write(kSurfacePrim, attribute, TensorOf(image));
            viewer->Sync();
        } catch (const std::invalid_argument& error) {
            scene.Clear(kSurfacePrim, attribute);
            message = error.what();
            LogWarning("viewer", "Cannot use detail ", kind, " ", path, ": ", message);
            return false;
        }
        message = std::to_string(image->width) + " x " + std::to_string(image->height);
        LogInfo("viewer", "Loaded detail ", kind, " ", path, " (", message, ")");
        return true;
    }

    void HandleSurfaceAction(SurfacePanelAction action) {
        switch (action) {
        case SurfacePanelAction::LoadDetailAlbedo:
            LoadDetail(false);
            break;
        case SurfacePanelAction::LoadDetailNormal:
            LoadDetail(true);
            break;
        case SurfacePanelAction::ClearDetail:
            if (scene.Has(kSurfacePrim)) {
                scene.Clear(kSurfacePrim, ernie::tokens::kDetailAlbedo);
                scene.Clear(kSurfacePrim, ernie::tokens::kDetailNormal);
            }
            surfacePanel.albedoStatus.clear();
            surfacePanel.normalStatus.clear();
            break;
        case SurfacePanelAction::None:
            break;
        }
    }

    /// Takes a finished generation and refreshes what the panels show, before the UI.
    void BeginFrame() {
        if (auto result = worker.TakeResult()) {
            ApplyResult(std::move(*result));
        }
        status.generating = worker.Busy();
        surfacePanel.hasAlbedoMap = scene.Read(kSurfacePrim, ernie::tokens::kAlbedoMap) != nullptr;
    }

    /// The keyboard shortcuts, inside the UI frame.
    void HandleShortcuts() {
        if (ImGui::GetIO().WantTextInput) {
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_H, false)) {
            const bool visible =
                std::none_of(panels.begin(), panels.end(),
                             [](const ernie::gui::Panel* panel) { return panel->IsVisible(); });
            for (ernie::gui::Panel* panel : panels) {
                panel->SetVisible(visible);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_L, false)) {
            log->SetVisible(!log->IsVisible());
        }
        if (ImGui::IsKeyPressed(ImGuiKey_R) && ValidateSettings(settings).empty()) {
            Request();
        }
    }

    void AddPanels() {
        using ernie::gui::Dock;
        using ernie::gui::Viewer;
        panels.push_back(&viewer->AddPanel("Terrain", Dock::Left, [this](Viewer&) {
            const bool generate = DrawTerrainPanel(settings, autoRegenerate, status);
            const bool changed = !(settings == requested);
            if (generate || (autoRegenerate && changed && ValidateSettings(settings).empty())) {
                Request();
            }
        }));
        panels.push_back(&viewer->AddPanel("View", Dock::Left, [this](Viewer& v) {
            if (DrawViewPanel(view)) {
                v.Frame();
            }
        }));
        panels.push_back(&viewer->AddPanel("Surface", Dock::Left, [this](Viewer&) {
            HandleSurfaceAction(DrawSurfacePanel(view.material, surfacePanel, Brdfs()));
        }));
        panels.push_back(
            &viewer->AddPanel("Info", Dock::Right, [this](Viewer&) { DrawInfoPanel(info); }));
        log = &viewer->AddPanel("Log", Dock::Bottom,
                                [this](Viewer&) { DrawLogPanel(logBuffer, logPanel); });
        panels.push_back(log);
        if (ernie::gui::Panel* stats = viewer->FindPanel("Stats")) {
            panels.push_back(stats);
        }
    }

    void AddCallbacks() {
        using ernie::gui::FrameInfo;
        using ernie::gui::Viewer;
        viewer->OnFrame([this](Viewer&, const FrameInfo&) { BeginFrame(); });
        viewer->OnUi([this](Viewer&, const FrameInfo&) { HandleShortcuts(); });
        viewer->OnPreRender([this](Viewer& v, const FrameInfo&) {
            AuthorView(scene, view, v.ActiveCamera(), Brdfs());
        });
    }
};

App::App(const AppOptions& options) : state_(std::make_unique<State>()) {
    State& s = *state_;
    s.options = options;
    SetLogLevel(options.logLevel);
    ernie::gui::ViewerOptions viewerOptions;
    viewerOptions.title = "TerrainGen Viewer";
    viewerOptions.width = 1600;
    viewerOptions.height = 1000;
    viewerOptions.hidden = options.hidden;
    viewerOptions.validation = options.validation;
    s.viewer = std::make_unique<ernie::gui::Viewer>(s.scene, viewerOptions);
    LogInfo("viewer", "TerrainGen ", TERRAINGEN_VERSION_STRING, " on ",
            s.viewer->Context().DeviceName(),
            s.viewer->Context().ValidationEnabled() ? " with Vulkan validation" : "");
    s.AddPanels();
    s.AddCallbacks();
    AuthorView(s.scene, s.view, s.viewer->ActiveCamera(), s.Brdfs());
    s.viewer->SetLighting(ernie::gui::Lighting::Stage);
    s.ApplyOptions();
    s.Request();
}

App::~App() = default;

void App::Run() {
    State& s = *state_;
    for (long frame = 0; frame != s.options.maxFrames && s.viewer->Step(); ++frame) {
    }
}

ernie::gui::Viewer& App::Viewer() {
    return *state_->viewer;
}

}  // namespace terraingen::viewer
