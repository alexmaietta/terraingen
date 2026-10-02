#include "app.h"

#include "terraingen/version.h"

#include "ernie/depth_buffer.h"
#include "ernie/gpu_context.h"
#include "ernie/orbit_camera.h"
#include "ernie/terrain_renderer.h"
#include "glfw_surface.h"
#include "panels.h"
#include "settings.h"
#include "worker.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_wgpu.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace terraingen::viewer {

namespace {

struct GlfwWindow {
    GLFWwindow* window = nullptr;

    GlfwWindow() {
        glfwSetErrorCallback([](int code, const char* message) {
            std::fprintf(stderr, "GLFW %d: %s\n", code, message);
        });
        if (glfwInit() == GLFW_FALSE) {
            throw std::runtime_error("Could not initialize GLFW");
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(1600, 1000, "TerrainGen Viewer", nullptr, nullptr);
        if (window == nullptr) {
            glfwTerminate();
            throw std::runtime_error("Could not create a window");
        }
    }

    ~GlfwWindow() {
        glfwDestroyWindow(window);
        glfwTerminate();
    }

    GlfwWindow(const GlfwWindow&) = delete;
    GlfwWindow& operator=(const GlfwWindow&) = delete;
};

struct ImGuiContext {
    ImGuiContext(GLFWwindow* window, const ernie::GpuContext& gpu) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForOther(window, true);
        ImGui_ImplWGPU_InitInfo info;
        info.Device = gpu.Device();
        info.RenderTargetFormat = gpu.SurfaceFormat();
        ImGui_ImplWGPU_Init(&info);
    }

    ~ImGuiContext() {
        ImGui_ImplWGPU_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    ImGuiContext(const ImGuiContext&) = delete;
    ImGuiContext& operator=(const ImGuiContext&) = delete;
};

WGPURenderPassEncoder BeginPass(WGPUCommandEncoder encoder, WGPUTextureView target, WGPULoadOp load,
                                const std::array<float, 3>& clear, WGPUTextureView depth) {
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = target;
    color.loadOp = load;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = {static_cast<double>(clear[0]), static_cast<double>(clear[1]),
                        static_cast<double>(clear[2]), 1.0};
    WGPURenderPassDepthStencilAttachment depthAttachment =
        WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depthAttachment.view = depth;
    depthAttachment.depthLoadOp = WGPULoadOp_Clear;
    depthAttachment.depthStoreOp = WGPUStoreOp_Discard;
    depthAttachment.depthClearValue = 1.0f;

    WGPURenderPassDescriptor descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    descriptor.colorAttachmentCount = 1;
    descriptor.colorAttachments = &color;
    descriptor.depthStencilAttachment = depth != nullptr ? &depthAttachment : nullptr;
    return wgpuCommandEncoderBeginRenderPass(encoder, &descriptor);
}

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
    GlfwWindow window;
    std::unique_ptr<ernie::GpuContext> gpu;
    std::optional<ImGuiContext> imgui;
    std::optional<ernie::DepthBuffer> depth;
    std::optional<ernie::TerrainRenderer> terrain;
    ernie::OrbitCamera camera;

    TerrainSettings settings;
    TerrainSettings requested;
    ViewSettings view;
    Worker worker;
    bool autoRegenerate = true;
    bool uiVisible = true;
    bool logVisible = true;
    bool framed = false;
    double framedExtent = 0.0;
    TerrainPanelStatus status;
    InfoPanelData info;
    SurfacePanelState surfacePanel;
    LogPanelState logPanel;

    int width = 0;
    int height = 0;

    /// Applies the command-line reflectance model and detail textures.
    /// @throws std::runtime_error for an unknown model or a texture that cannot be loaded.
    void ApplyOptions() {
        if (!options.brdf.empty()) {
            if (terrain->Brdfs().Find(options.brdf) == nullptr) {
                std::string known;
                for (const ernie::BrdfModel& model : terrain->Brdfs().Models()) {
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

    void Resize(int newWidth, int newHeight) {
        width = newWidth;
        height = newHeight;
        if (width > 0 && height > 0) {
            gpu->ConfigureSurface(static_cast<std::uint32_t>(width),
                                  static_cast<std::uint32_t>(height));
            depth->Resize(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
        }
    }

    void Frame() {
        if (terrain->HasHeightfield()) {
            const auto [low, high] = terrain->Bounds(view.exaggeration);
            camera.Frame(low, high);
        }
    }

    void ApplyResult(GenerationResult result) {
        status.error = result.error;
        if (!result.error.empty()) {
            LogWarning("viewer", "Generation ", result.id, " failed: ", result.error);
            return;
        }
        const Stopwatch upload;
        try {
            terrain->SetHeightfield(ViewOf(result.raster));
        } catch (const std::invalid_argument& error) {
            status.error = error.what();
            LogWarning("viewer", "Cannot display generation ", result.id, ": ", status.error);
            return;
        }
        LogInfo("viewer", "Generation ", result.id, " finished in ",
                FormatDuration(result.milliseconds), "; uploaded ", result.raster.Width(), "x",
                result.raster.Height(), " heightfield in ", FormatDuration(upload.Milliseconds()));
        info.width = terrain->Width();
        info.height = terrain->Height();
        info.gpuBytes = terrain->GpuBytes();
        info.stats = result.stats;
        info.generationMilliseconds = result.milliseconds;

        const double extent =
            static_cast<double>(std::max(result.settings.width, result.settings.height)) *
            result.settings.pixelSize;
        if (!framed || extent != framedExtent) {
            Frame();
            framed = true;
            framedExtent = extent;
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
        const ernie::Image& image = std::get<ernie::Image>(loaded);
        try {
            if (normalMap) {
                terrain->Material().SetDetailNormal(&image);
            } else {
                terrain->Material().SetDetailAlbedo(&image);
            }
        } catch (const std::invalid_argument& error) {
            message = error.what();
            LogWarning("viewer", "Cannot use detail ", kind, " ", path, ": ", message);
            return false;
        }
        message = std::to_string(image.width) + " x " + std::to_string(image.height);
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
            terrain->Material().SetDetailTextures(nullptr, nullptr);
            surfacePanel.albedoStatus.clear();
            surfacePanel.normalStatus.clear();
            break;
        case SurfacePanelAction::None:
            break;
        }
    }

    void HandleInput() {
        const ImGuiIO& io = ImGui::GetIO();
        if (!io.WantCaptureMouse) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
                camera.Orbit(-io.MouseDelta.x * 0.005f, io.MouseDelta.y * 0.005f);
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) ||
                ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
                camera.Pan(io.MouseDelta.x, io.MouseDelta.y, io.DisplaySize.y);
            }
            if (io.MouseWheel != 0.0f) {
                camera.Zoom(io.MouseWheel);
            }
        }
        if (!io.WantCaptureKeyboard) {
            if (ImGui::IsKeyPressed(ImGuiKey_F)) {
                Frame();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_H, false)) {
                uiVisible = !uiVisible;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_L, false)) {
                logVisible = !logVisible;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_R) && ValidateSettings(settings).empty()) {
                Request();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                glfwSetWindowShouldClose(window.window, GLFW_TRUE);
            }
        }
    }

    void DrawUi() {
        status.generating = worker.Busy();
        surfacePanel.hasAlbedoMap = terrain->Material().HasAlbedoMap();
        info.gpu = gpu->AdapterDescription();
        info.gpuBytes = terrain->GpuBytes();
        info.triangles = terrain->TriangleCount();
        info.patches = terrain->PatchCount();
        info.lodLevels = terrain->Lod().Levels();
        if (!uiVisible) {
            return;
        }
        const SidebarActions actions = DrawSidebar(settings, autoRegenerate, status, view,
                                                   surfacePanel, terrain->Brdfs(), info);
        const bool changed = !(settings == requested);
        if (actions.generate || (autoRegenerate && changed && ValidateSettings(settings).empty())) {
            Request();
        }
        if (actions.frame) {
            Frame();
        }
        HandleSurfaceAction(actions.surface);
        if (logVisible) {
            DrawLogPanel(logBuffer, logPanel, actions.width);
        }
    }

    void Render(WGPUTexture target) {
        WGPUTextureView targetView = wgpuTextureCreateView(target, nullptr);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->Device(), nullptr);

        ernie::TerrainDrawParams params;
        params.camera = camera.MakeView(static_cast<float>(width) / static_cast<float>(height));
        params.sunDirection = SunDirection(view.sunAzimuthDegrees, view.sunElevationDegrees);
        params.ambient = view.ambient;
        params.exaggeration = view.exaggeration;
        params.colorMode = view.colorMode;
        params.wireframe = view.wireframe;
        params.shadows = view.shadows;
        params.lod = view.lod;
        params.surface = view.material.Effective(terrain->Brdfs());
        terrain->Update(params);
        terrain->EncodeShadows(encoder);

        WGPURenderPassEncoder scene =
            BeginPass(encoder, targetView, WGPULoadOp_Clear, view.background, depth->View());
        terrain->Draw(scene);
        wgpuRenderPassEncoderEnd(scene);

        WGPURenderPassEncoder overlay =
            BeginPass(encoder, targetView, WGPULoadOp_Load, view.background, nullptr);
        ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), overlay);
        wgpuRenderPassEncoderEnd(overlay);

        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(gpu->Queue(), 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuRenderPassEncoderRelease(overlay);
        wgpuRenderPassEncoderRelease(scene);
        wgpuCommandEncoderRelease(encoder);
        wgpuTextureViewRelease(targetView);
    }
};

App::App(const AppOptions& options) : state_(std::make_unique<State>()) {
    State& s = *state_;
    s.options = options;
    SetLogLevel(options.logLevel);
    GLFWwindow* window = s.window.window;
    s.gpu = ernie::GpuContext::Create(
        [window](WGPUInstance instance) { return CreateGlfwSurface(instance, window); });
    LogInfo("viewer", "TerrainGen ", TERRAINGEN_VERSION_STRING, " on ",
            s.gpu->AdapterDescription());
    s.imgui.emplace(window, *s.gpu);
    s.depth.emplace(s.gpu->Device());
    s.terrain.emplace(*s.gpu, s.gpu->SurfaceFormat(), s.depth->Format());
    s.ApplyOptions();

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    s.Resize(width, height);
    s.Request();
}

App::~App() = default;

void App::Run() {
    State& s = *state_;
    for (long frame = 0; !glfwWindowShouldClose(s.window.window) && frame != s.options.maxFrames;
         ++frame) {
        glfwPollEvents();
        if (glfwGetWindowAttrib(s.window.window, GLFW_ICONIFIED) != 0) {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(s.window.window, &width, &height);
        if (width != s.width || height != s.height) {
            s.Resize(width, height);
        }
        if (width <= 0 || height <= 0) {
            continue;
        }
        if (auto result = s.worker.TakeResult()) {
            s.ApplyResult(std::move(*result));
        }

        WGPUSurfaceTexture surfaceTexture = WGPU_SURFACE_TEXTURE_INIT;
        wgpuSurfaceGetCurrentTexture(s.gpu->Surface(), &surfaceTexture);
        if (ImGui_ImplWGPU_IsSurfaceStatusError(surfaceTexture.status)) {
            throw std::runtime_error("Unrecoverable WebGPU surface status");
        }
        if (ImGui_ImplWGPU_IsSurfaceStatusSubOptimal(surfaceTexture.status)) {
            if (surfaceTexture.texture != nullptr) {
                wgpuTextureRelease(surfaceTexture.texture);
            }
            s.Resize(width, height);
            continue;
        }

        ImGui_ImplWGPU_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        s.HandleInput();
        s.DrawUi();
        ImGui::Render();

        s.Render(surfaceTexture.texture);
        wgpuSurfacePresent(s.gpu->Surface());
        wgpuTextureRelease(surfaceTexture.texture);
    }
}

}  // namespace terraingen::viewer
