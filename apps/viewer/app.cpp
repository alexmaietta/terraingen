#include "app.h"

#include "terraingen/core/version.h"

#include "ernie/camera/orbit_camera.h"
#include "ernie/gpu/gpu_image.h"
#include "ernie/gpu/pipeline.h"
#include "ernie/gpu/swapchain.h"
#include "ernie/gpu/vulkan_context.h"
#include "ernie/raster/raster_delegate.h"
#include "ernie/scene/scene.h"
#include "ernie/scene/tokens.h"
#include "glfw_surface.h"
#include "panels.h"
#include "settings.h"
#include "worker.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
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
        // GLFW uses Ernie's Vulkan, which on macOS may be MoltenVK without a loader.
        glfwInitVulkanLoader(ernie::LoadVulkanLoader());
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
    ImGuiContext(GLFWwindow* window, const ernie::VulkanContext& gpu,
                 const ernie::Swapchain& swapchain) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForVulkan(window, true);
        const VkFormat format = swapchain.Format();
        ImGui_ImplVulkan_InitInfo info{};
        info.ApiVersion = VK_API_VERSION_1_3;
        info.Instance = gpu.Instance();
        info.PhysicalDevice = gpu.PhysicalDevice();
        info.Device = gpu.Device();
        info.QueueFamily = gpu.QueueFamily();
        info.Queue = gpu.Queue();
        info.DescriptorPoolSize = 64;
        info.MinImageCount = swapchain.MinImageCount();
        info.ImageCount = swapchain.ImageCount();
        info.UseDynamicRendering = true;
        info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        info.PipelineInfoMain.PipelineRenderingCreateInfo = {
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &format;
        ImGui_ImplVulkan_Init(&info);
    }

    ~ImGuiContext() {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    ImGuiContext(const ImGuiContext&) = delete;
    ImGuiContext& operator=(const ImGuiContext&) = delete;
};

/// Command buffer and synchronisation for the one frame the viewer keeps in flight.
struct FrameResources {
    const ernie::VulkanContext& gpu;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkFence done = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;

    explicit FrameResources(const ernie::VulkanContext& context) : gpu(context) {
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = gpu.QueueFamily();
        ernie::CheckVk(vkCreateCommandPool(gpu.Device(), &poolInfo, nullptr, &pool),
                       "vkCreateCommandPool");
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = pool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        ernie::CheckVk(vkAllocateCommandBuffers(gpu.Device(), &allocate, &commands),
                       "vkAllocateCommandBuffers");
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        ernie::CheckVk(vkCreateFence(gpu.Device(), &fenceInfo, nullptr, &done), "vkCreateFence");
        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        ernie::CheckVk(vkCreateSemaphore(gpu.Device(), &semaphoreInfo, nullptr, &acquired),
                       "vkCreateSemaphore");
    }

    ~FrameResources() {
        vkDeviceWaitIdle(gpu.Device());
        vkDestroySemaphore(gpu.Device(), acquired, nullptr);
        vkDestroyFence(gpu.Device(), done, nullptr);
        vkDestroyCommandPool(gpu.Device(), pool, nullptr);
    }

    FrameResources(const FrameResources&) = delete;
    FrameResources& operator=(const FrameResources&) = delete;

    /// Waits until the previous frame has finished on the GPU.
    void Wait() const {
        vkWaitForFences(gpu.Device(), 1, &done, VK_TRUE, UINT64_MAX);
    }
};

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
    std::unique_ptr<ernie::VulkanContext> gpu;
    std::unique_ptr<ernie::Swapchain> swapchain;
    std::unique_ptr<ernie::GpuImage> depth;
    std::optional<FrameResources> frame;
    std::optional<ImGuiContext> imgui;
    std::optional<ernie::RasterDelegate> delegate;
    ernie::Scene scene;
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

    const ernie::TerrainPart& Terrain() const {
        return delegate->Terrain();
    }

    const ernie::BrdfRegistry& Brdfs() const {
        return delegate->Renderer().Brdfs();
    }

    std::uint64_t GpuBytes() const {
        return delegate->Renderer().GpuBytes() + Terrain().GpuBytes();
    }

    void Request() {
        requested = settings;
        worker.Request(settings);
    }

    void Resize(int newWidth, int newHeight) {
        width = newWidth;
        height = newHeight;
        if (width > 0 && height > 0) {
            swapchain->Resize(static_cast<std::uint32_t>(width),
                              static_cast<std::uint32_t>(height));
            CreateDepth();
            ImGui_ImplVulkan_SetMinImageCount(swapchain->MinImageCount());
        }
    }

    void CreateDepth() {
        const VkExtent2D extent = swapchain->Extent();
        depth = std::make_unique<ernie::GpuImage>(
            *gpu, ernie::ImageDesc{extent.width, extent.height, 1, 1, VK_FORMAT_D32_SFLOAT,
                                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT});
    }

    void Frame() {
        if (Terrain().HasHeightfield()) {
            const auto [low, high] = Terrain().Bounds(view.exaggeration);
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
            AuthorTerrain(scene, std::make_shared<const Raster<float>>(std::move(result.raster)));
            delegate->Sync(scene);
        } catch (const std::invalid_argument& error) {
            status.error = error.what();
            LogWarning("viewer", "Cannot display generation ", result.id, ": ", status.error);
            return;
        }
        info.width = Terrain().Width();
        info.height = Terrain().Height();
        LogInfo("viewer", "Generation ", result.id, " finished in ",
                FormatDuration(result.milliseconds), "; uploaded ", info.width, "x", info.height,
                " heightfield in ", FormatDuration(upload.Milliseconds()));
        info.gpuBytes = GpuBytes();
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
        auto image =
            std::make_shared<const ernie::Image>(std::move(std::get<ernie::Image>(loaded)));
        const std::string_view attribute =
            normalMap ? ernie::tokens::kDetailNormal : ernie::tokens::kDetailAlbedo;
        try {
            scene.Define(kSurfacePrim, ernie::tokens::kMaterial);
            scene.Define(kTerrainPrim, ernie::tokens::kTerrain)
                .Set(ernie::tokens::kMaterialBinding, ernie::Path(kSurfacePrim));
            scene.Write(kSurfacePrim, attribute, TensorOf(image));
            delegate->Sync(scene);
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
        const std::optional<std::uint32_t> surface = delegate->MaterialId(kSurfacePrim);
        surfacePanel.hasAlbedoMap =
            surface.has_value() && delegate->Renderer().Material(*surface).HasAlbedoMap();
        info.gpu = gpu->DeviceName();
        info.gpuBytes = GpuBytes();
        info.triangles = Terrain().TriangleCount() + delegate->Meshes().TriangleCount();
        info.patches = Terrain().PatchCount();
        info.lodLevels = Terrain().Lod().Levels();
        if (!uiVisible) {
            return;
        }
        const SidebarActions actions =
            DrawSidebar(settings, autoRegenerate, status, view, surfacePanel, Brdfs(), info);
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

    void Render(const ernie::Swapchain::Frame& image) {
        AuthorView(scene, view, camera, Brdfs());
        delegate->Update(scene, kSettingsPrim, swapchain->Extent());

        VkCommandBuffer commands = frame->commands;
        vkResetCommandBuffer(commands, 0);
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commands, &begin);
        ernie::TransitionImage(commands, image.image, VK_IMAGE_ASPECT_COLOR_BIT,
                               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        depth->Transition(commands, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
        ernie::RenderingTarget target;
        target.color = image.view;
        target.depth = depth->View();
        target.extent = swapchain->Extent();
        delegate->Record(commands, target);

        ernie::RenderingTarget overlay;
        overlay.color = image.view;
        overlay.extent = swapchain->Extent();
        overlay.loadColor = true;
        ernie::BeginRendering(commands, overlay);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commands);
        vkCmdEndRendering(commands);
        ernie::TransitionImage(commands, image.image, VK_IMAGE_ASPECT_COLOR_BIT,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        vkEndCommandBuffer(commands);

        VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        wait.semaphore = image.acquired;
        wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        signal.semaphore = image.rendered;
        signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commandInfo.commandBuffer = commands;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.waitSemaphoreInfoCount = 1;
        submit.pWaitSemaphoreInfos = &wait;
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &commandInfo;
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &signal;
        vkResetFences(gpu->Device(), 1, &frame->done);
        ernie::CheckVk(vkQueueSubmit2(gpu->Queue(), 1, &submit, frame->done), "vkQueueSubmit2");
    }
};

App::App(const AppOptions& options) : state_(std::make_unique<State>()) {
    State& s = *state_;
    s.options = options;
    SetLogLevel(options.logLevel);
    GLFWwindow* window = s.window.window;
    ernie::VulkanContextOptions gpuOptions;
    gpuOptions.validation = options.validation;
    gpuOptions.instanceExtensions = GlfwInstanceExtensions();
    gpuOptions.deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    s.gpu = ernie::VulkanContext::Create(gpuOptions);
    LogInfo("viewer", "TerrainGen ", TERRAINGEN_VERSION_STRING, " on ", s.gpu->DeviceName(),
            s.gpu->ValidationEnabled() ? " with Vulkan validation" : "");

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    s.swapchain =
        std::make_unique<ernie::Swapchain>(*s.gpu, CreateGlfwSurface(s.gpu->Instance(), window),
                                           static_cast<std::uint32_t>(std::max(width, 1)),
                                           static_cast<std::uint32_t>(std::max(height, 1)));
    s.width = width;
    s.height = height;
    s.CreateDepth();
    s.frame.emplace(*s.gpu);
    s.imgui.emplace(window, *s.gpu, *s.swapchain);
    s.delegate.emplace(*s.gpu, s.swapchain->Format(), VK_FORMAT_D32_SFLOAT);
    s.ApplyOptions();
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
        s.frame->Wait();
        if (auto result = s.worker.TakeResult()) {
            s.ApplyResult(std::move(*result));
        }
        const std::optional<ernie::Swapchain::Frame> image =
            s.swapchain->Acquire(s.frame->acquired);
        if (!image) {
            s.Resize(width, height);
            continue;
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        s.HandleInput();
        s.DrawUi();
        ImGui::Render();

        s.Render(*image);
        if (!s.swapchain->Present(*image)) {
            s.Resize(width, height);
        }
    }
}

}  // namespace terraingen::viewer
