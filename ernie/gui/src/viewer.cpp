#include "ernie/gui/viewer.h"

#include "ernie/camera/camera.h"
#include "ernie/camera/orbit_controller.h"
#include "ernie/camera/sensor_filters.h"
#include "ernie/gpu/gpu_image.h"
#include "ernie/gpu/offscreen_target.h"
#include "ernie/gpu/pipeline.h"
#include "ernie/gpu/swapchain.h"
#include "ernie/gpu/vulkan_context.h"
#include "ernie/render/render_delegate.h"
#include "ernie/render/resolve.h"
#include "ernie/scene/schema.h"
#include "ernie/scene/tokens.h"
#include "icons.h"
#include "session.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>
#include <implot.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

extern "C" {
extern const unsigned char kErnieFont_inter[];
extern const std::size_t kErnieFont_inter_size;
extern const unsigned char kErnieFont_lucide[];
extern const std::size_t kErnieFont_lucide_size;
}

namespace ernie::gui {

namespace {

constexpr VkFormat kViewportFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr const char* kViewportTitle = "Viewport";
constexpr const char* kStatsTitle = "Stats";
constexpr const char* kDockSpace = "ErnieDockSpace";
constexpr std::size_t kFrameHistory = 240;

const Path& SessionSettingsPath() {
    static const Path path = SessionRootPath().Child("RenderSettings");
    return path;
}

/// GLFW is process-wide: initialised by the first visible viewer, terminated with the last.
class GlfwLibrary {
  public:
    GlfwLibrary() {
        if (users_++ == 0) {
            glfwSetErrorCallback([](int code, const char* message) {
                std::fprintf(stderr, "GLFW %d: %s\n", code, message);
            });
            glfwInitVulkanLoader(LoadVulkanLoader());
            if (glfwInit() == GLFW_FALSE) {
                users_ = 0;
                throw std::runtime_error("Could not initialize GLFW");
            }
        }
    }

    ~GlfwLibrary() {
        if (--users_ == 0) {
            glfwTerminate();
        }
    }

    GlfwLibrary(const GlfwLibrary&) = delete;
    GlfwLibrary& operator=(const GlfwLibrary&) = delete;

  private:
    static inline int users_ = 0;
};

/// The command buffer, fence and acquire semaphore of the one frame kept in flight.
struct FrameResources {
    const VulkanContext& gpu;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkFence done = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;

    explicit FrameResources(const VulkanContext& context) : gpu(context) {
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = gpu.QueueFamily();
        CheckVk(vkCreateCommandPool(gpu.Device(), &poolInfo, nullptr, &pool),
                "vkCreateCommandPool");
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = pool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        CheckVk(vkAllocateCommandBuffers(gpu.Device(), &allocate, &commands),
                "vkAllocateCommandBuffers");
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        CheckVk(vkCreateFence(gpu.Device(), &fenceInfo, nullptr, &done), "vkCreateFence");
        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        CheckVk(vkCreateSemaphore(gpu.Device(), &semaphoreInfo, nullptr, &acquired),
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

    void Wait() const {
        vkWaitForFences(gpu.Device(), 1, &done, VK_TRUE, UINT64_MAX);
    }
};

ImVec4 ToImVec4(const glm::vec4& colour) {
    return {colour.r, colour.g, colour.b, colour.a};
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!std::filesystem::is_regular_file(path) || !file) {
        throw std::runtime_error("Cannot read " + path.string());
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

/// The last path component, for labels.
std::string Label(const Path& path) {
    return std::string(path.Name());
}

const char* LightingName(Lighting lighting) {
    switch (lighting) {
    case Lighting::Stage:
        return "Stage Lights";
    case Lighting::Camera:
        return "Camera Light";
    case Lighting::Default:
        return "Default Light";
    }
    return "";
}

/// Built-in panel showing the device, frame times, memory and the last render error.
class StatsPanel final : public Panel {
  public:
    StatsPanel() : Panel(kStatsTitle) {}

    void Record(float milliseconds) {
        times_.push_back(milliseconds);
        if (times_.size() > kFrameHistory) {
            times_.pop_front();
        }
    }

    void Draw(Viewer& viewer) override;

  private:
    std::deque<float> times_;
};

}  // namespace

struct Viewer::State {
    Viewer& self;
    Scene& scene;
    ViewerOptions options;

    std::optional<GlfwLibrary> glfw;
    GLFWwindow* window = nullptr;
    std::unique_ptr<VulkanContext> gpu;
    std::unique_ptr<Swapchain> swapchain;
    std::unique_ptr<OffscreenTarget> uiTarget;
    std::optional<FrameResources> frame;
    ImGuiContext* imgui = nullptr;
    ImPlotContext* implot = nullptr;
    bool imguiVulkan = false;
    bool imguiGlfw = false;
    std::unique_ptr<RenderDelegate> delegate;

    std::unique_ptr<OffscreenTarget> viewport;
    std::unique_ptr<Sampler> sampler;
    VkDescriptorSet viewportTexture = VK_NULL_HANDLE;
    VkExtent2D requestedViewport{640, 360};

    OrbitController controller;
    Path activeCamera;
    Path settingsSource;
    std::map<std::string, std::uint64_t> mirrored;
    std::string viewVar = "LdrColor";
    Lighting lighting = Lighting::Stage;
    Theme theme;
    float contentScale = 1.0f;
    std::vector<Path> selection;

    struct FrameCallback {
        std::uint64_t id;
        std::function<void(Viewer&, const FrameInfo&)> callback;
    };
    struct SelectionCallback {
        std::uint64_t id;
        std::function<void(Viewer&, std::span<const Path>)> callback;
    };
    struct MenuItem {
        std::string menu;
        std::string label;
        std::function<void(Viewer&)> action;
    };
    std::vector<FrameCallback> frameCallbacks;
    std::vector<SelectionCallback> selectionCallbacks;
    std::uint64_t nextCallback = 1;
    std::vector<std::unique_ptr<Panel>> panels;
    StatsPanel* stats = nullptr;
    std::vector<MenuItem> menuItems;

    std::uint64_t frames = 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last = start;
    bool open = true;
    bool layoutReady = false;
    ImGuiID sideDock = 0;
    std::optional<std::string> pendingLayout;
    std::string renderError;

    struct CaptureRequest {
        std::filesystem::path path;
        bool rendered = false;
    };
    std::optional<CaptureRequest> capture;

    int framebufferWidth = 0;
    int framebufferHeight = 0;

    State(Viewer& viewer, Scene& s, const ViewerOptions& o)
        : self(viewer), scene(s), options(o), theme(o.theme) {}

    ~State() {
        if (gpu) {
            vkDeviceWaitIdle(gpu->Device());
        }
        if (viewportTexture != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(viewportTexture);
        }
        viewport.reset();
        sampler.reset();
        delegate.reset();
        if (imguiVulkan) {
            ImGui_ImplVulkan_Shutdown();
        }
        if (imguiGlfw) {
            ImGui_ImplGlfw_Shutdown();
        }
        if (implot != nullptr) {
            ImPlot::DestroyContext(implot);
        }
        if (imgui != nullptr) {
            ImGui::DestroyContext(imgui);
        }
        frame.reset();
        uiTarget.reset();
        swapchain.reset();
        gpu.reset();
        if (window != nullptr) {
            glfwDestroyWindow(window);
        }
    }

    State(const State&) = delete;
    State& operator=(const State&) = delete;

    void OpenWindow() {
        glfw.emplace();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(static_cast<int>(options.width), static_cast<int>(options.height),
                                  options.title.c_str(), nullptr, nullptr);
        if (window == nullptr) {
            throw std::runtime_error("Could not create a window");
        }
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        glfwGetWindowContentScale(window, &scaleX, &scaleY);
        contentScale = std::max(scaleX, 1.0f);
    }

    void CreateGpu() {
        VulkanContextOptions gpuOptions;
        gpuOptions.validation = options.validation;
        gpuOptions.preferredDevice = options.device;
        if (window != nullptr) {
            std::uint32_t count = 0;
            const char** names = glfwGetRequiredInstanceExtensions(&count);
            if (names == nullptr) {
                throw std::runtime_error("GLFW cannot present with Vulkan on this system");
            }
            gpuOptions.instanceExtensions.assign(names, names + count);
            gpuOptions.deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        }
        gpu = VulkanContext::Create(gpuOptions);
        if (window != nullptr) {
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            const VkResult result =
                glfwCreateWindowSurface(gpu->Instance(), window, nullptr, &surface);
            CheckVk(result, "glfwCreateWindowSurface");
            glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
            swapchain = std::make_unique<Swapchain>(
                *gpu, surface, static_cast<std::uint32_t>(std::max(framebufferWidth, 1)),
                static_cast<std::uint32_t>(std::max(framebufferHeight, 1)));
        } else {
            framebufferWidth = static_cast<int>(options.width);
            framebufferHeight = static_cast<int>(options.height);
            uiTarget = std::make_unique<OffscreenTarget>(*gpu, options.width, options.height);
        }
        frame.emplace(*gpu);
        delegate = CreateRenderDelegate("raster", *gpu, kViewportFormat);
        sampler = std::make_unique<Sampler>(*gpu, SamplerDesc{VK_FILTER_LINEAR});
    }

    VkFormat UiFormat() const {
        return swapchain ? swapchain->Format() : uiTarget->ColorFormat();
    }

    void CreateImGui() {
        IMGUI_CHECKVERSION();
        imgui = ImGui::CreateContext();
        implot = ImPlot::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
        LoadFonts();
        ApplyTheme(theme, contentScale, ImGui::GetStyle());
        if (window != nullptr) {
            ImGui_ImplGlfw_InitForVulkan(window, true);
            imguiGlfw = true;
        } else {
            io.DisplaySize =
                ImVec2(static_cast<float>(options.width), static_cast<float>(options.height));
            io.BackendPlatformName = "ernie_hidden";
        }
        const VkFormat format = UiFormat();
        ImGui_ImplVulkan_InitInfo info{};
        info.ApiVersion = VK_API_VERSION_1_3;
        info.Instance = gpu->Instance();
        info.PhysicalDevice = gpu->PhysicalDevice();
        info.Device = gpu->Device();
        info.QueueFamily = gpu->QueueFamily();
        info.Queue = gpu->Queue();
        info.DescriptorPoolSize = 64;
        info.MinImageCount = swapchain ? swapchain->MinImageCount() : 2;
        info.ImageCount = swapchain ? swapchain->ImageCount() : 2;
        info.UseDynamicRendering = true;
        info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        info.PipelineInfoMain.PipelineRenderingCreateInfo = {
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &format;
        ImGui_ImplVulkan_Init(&info);
        imguiVulkan = true;
        if (!options.layout.empty()) {
            pendingLayout = ReadFile(options.layout);
        }
    }

    void LoadFonts() {
        ImGuiIO& io = ImGui::GetIO();
        ImFontConfig text;
        text.FontDataOwnedByAtlas = false;
        std::snprintf(text.Name, sizeof(text.Name), "Inter");
        io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kErnieFont_inter),
                                       static_cast<int>(kErnieFont_inter_size), theme.fontSize,
                                       &text);
        static const ImWchar kIconRange[] = {static_cast<ImWchar>(icons::kFirst),
                                             static_cast<ImWchar>(icons::kLast), 0};
        ImFontConfig iconConfig;
        iconConfig.FontDataOwnedByAtlas = false;
        iconConfig.MergeMode = true;
        iconConfig.GlyphMinAdvanceX = theme.iconSize;
        iconConfig.GlyphOffset = ImVec2(0.0f, 2.0f);
        std::snprintf(iconConfig.Name, sizeof(iconConfig.Name), "Lucide");
        io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kErnieFont_lucide),
                                       static_cast<int>(kErnieFont_lucide_size), theme.iconSize,
                                       &iconConfig, kIconRange);
    }

    void CreateSession() {
        settingsSource = options.settings;
        if (!settingsSource.IsEmpty() &&
            (!scene.Has(settingsSource) ||
             scene.TypeOf(settingsSource) != tokens::kRenderSettings)) {
            throw std::invalid_argument("'" + settingsSource.String() +
                                        "' is not a RenderSettings prim");
        }
        if (settingsSource.IsEmpty()) {
            settingsSource = FirstUserSettings();
        }
        activeCamera = PerspectiveCamera();
        lighting = HasStageLights(scene) ? Lighting::Stage : Lighting::Default;
        delegate->Sync(scene);
        FrameBounds(SceneBounds());
    }

    Path FirstUserSettings() const {
        for (const Path& path : scene.Prims(tokens::kRenderSettings)) {
            if (!IsSessionPath(path)) {
                return path;
            }
        }
        return {};
    }

    std::optional<Bounds3> SceneBounds() const {
        return delegate->SceneBounds();
    }

    /// Aims the controller at `bounds` (a unit box at the origin without any) and writes the
    /// active camera.
    void FrameBounds(const std::optional<Bounds3>& bounds) {
        const Bounds3 box = bounds.value_or(Bounds3{glm::vec3(-1.0f), glm::vec3(1.0f)});
        controller.Frame(box.first, box.second);
        WriteActiveCamera();
    }

    void WriteActiveCamera() {
        if (activeCamera == PerspectiveCamera()) {
            AuthorControllerCamera(scene, activeCamera, controller, ViewportWidth(),
                                   ViewportHeight());
        } else {
            scene.Set(activeCamera, tokens::kTransform,
                      LocalFromWorld(scene, activeCamera, controller.Transform()));
        }
    }

    std::uint32_t ViewportWidth() const {
        return viewport ? viewport->Extent().width : requestedViewport.width;
    }

    std::uint32_t ViewportHeight() const {
        return viewport ? viewport->Extent().height : requestedViewport.height;
    }

    void SetActive(const Path& camera) {
        activeCamera = camera;
        if (camera != PerspectiveCamera()) {
            const float fov = static_cast<float>(
                CameraOf(scene, camera, ViewportWidth(), ViewportHeight()).VerticalFov());
            controller =
                ControllerFromTransform(scene.WorldTransform(camera), controller.distance, fov);
        } else if (scene.Has(camera)) {
            const Camera current = CameraOf(scene, camera, ViewportWidth(), ViewportHeight());
            controller = ControllerFromTransform(current.transform, controller.distance,
                                                 static_cast<float>(current.VerticalFov()));
        }
    }

    /// Falls back to the session camera and the scene's first settings when the scene removed
    /// what the viewer was using.
    void ValidateSession() {
        if (!scene.Has(PerspectiveCamera())) {
            AuthorControllerCamera(scene, PerspectiveCamera(), controller, ViewportWidth(),
                                   ViewportHeight());
        }
        if (!scene.Has(activeCamera) || scene.TypeOf(activeCamera) != tokens::kCamera) {
            activeCamera = PerspectiveCamera();
        }
        if (!settingsSource.IsEmpty() &&
            (!scene.Has(settingsSource) ||
             scene.TypeOf(settingsSource) != tokens::kRenderSettings)) {
            settingsSource = FirstUserSettings();
        }
    }

    void ResizeSwapchain(int width, int height) {
        framebufferWidth = width;
        framebufferHeight = height;
        if (width > 0 && height > 0) {
            swapchain->Resize(static_cast<std::uint32_t>(width),
                              static_cast<std::uint32_t>(height));
            ImGui_ImplVulkan_SetMinImageCount(swapchain->MinImageCount());
        }
    }

    void ResizeViewport() {
        const VkExtent2D want{std::max(requestedViewport.width, 1u),
                              std::max(requestedViewport.height, 1u)};
        if (viewport && viewport->Extent().width == want.width &&
            viewport->Extent().height == want.height) {
            return;
        }
        vkDeviceWaitIdle(gpu->Device());
        if (viewportTexture != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(viewportTexture);
            viewportTexture = VK_NULL_HANDLE;
        }
        viewport =
            std::make_unique<OffscreenTarget>(*gpu, want.width, want.height, kViewportFormat);
        viewportTexture = ImGui_ImplVulkan_AddTexture(sampler->Handle(), viewport->Color().View(),
                                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (activeCamera == PerspectiveCamera()) {
            WriteActiveCamera();
        }
    }

    void SetSelection(std::vector<Path> paths) {
        if (paths == selection) {
            return;
        }
        selection = std::move(paths);
        const auto callbacks = selectionCallbacks;
        for (const SelectionCallback& entry : callbacks) {
            entry.callback(self, selection);
        }
    }

    void BuildDefaultLayout(ImGuiID dockSpace) {
        ImGui::DockBuilderRemoveNode(dockSpace);
        ImGui::DockBuilderAddNode(dockSpace, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockSpace, ImGui::GetMainViewport()->WorkSize);
        ImGuiID center = dockSpace;
        sideDock = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, nullptr, &center);
        ImGui::DockBuilderDockWindow(kViewportTitle, center);
        for (const auto& panel : panels) {
            ImGui::DockBuilderDockWindow(panel->Title().c_str(), sideDock);
        }
        ImGui::DockBuilderFinish(dockSpace);
    }

    void DrawMenuBar() {
        if (!ImGui::BeginMainMenuBar()) {
            return;
        }
        std::vector<std::string> menus = {"File", "Window"};
        for (const MenuItem& item : menuItems) {
            if (std::find(menus.begin(), menus.end(), item.menu) == menus.end()) {
                menus.push_back(item.menu);
            }
        }
        for (const std::string& menu : menus) {
            if (!ImGui::BeginMenu(menu.c_str())) {
                continue;
            }
            if (menu == "Window") {
                for (const auto& panel : panels) {
                    bool visible = panel->IsVisible();
                    if (ImGui::MenuItem(panel->Title().c_str(), nullptr, &visible)) {
                        panel->SetVisible(visible);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Reset Layout")) {
                    layoutReady = false;
                }
            }
            bool first = true;
            for (std::size_t i = 0; i < menuItems.size(); ++i) {
                if (menuItems[i].menu != menu) {
                    continue;
                }
                if (first && (menu == "Window" || menu == "File")) {
                    if (menu == "Window") {
                        ImGui::Separator();
                    }
                    first = false;
                }
                if (ImGui::MenuItem(menuItems[i].label.c_str())) {
                    const auto action = menuItems[i].action;
                    action(self);
                }
            }
            if (menu == "File") {
                if (!first) {
                    ImGui::Separator();
                }
                if (ImGui::MenuItem("Close", "Esc")) {
                    open = false;
                }
            }
            ImGui::EndMenu();
        }
        const std::string device = gpu->DeviceName();
        const float width = ImGui::CalcTextSize(device.c_str()).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - width - ImGui::GetStyle().ItemSpacing.x * 2.0f);
        ImGui::TextDisabled("%s", device.c_str());
        ImGui::EndMainMenuBar();
    }

    void DrawViewport() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const bool visible =
            ImGui::Begin(kViewportTitle, nullptr,
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        if (!visible) {
            ImGui::End();
            return;
        }
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImGui::GetContentRegionAvail();
        requestedViewport = {static_cast<std::uint32_t>(std::max(size.x, 1.0f)),
                             static_cast<std::uint32_t>(std::max(size.y, 1.0f))};
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                            ImGui::GetColorU32(ToImVec4(theme.background)));
        if (viewportTexture != VK_NULL_HANDLE) {
            draw->AddImage(ImTextureRef(reinterpret_cast<ImTextureID>(viewportTexture)), origin,
                           ImVec2(origin.x + size.x, origin.y + size.y));
        }
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##viewport", ImVec2(std::max(size.x, 1.0f), std::max(size.y, 1.0f)),
                               ImGuiButtonFlags_MouseButtonLeft |
                                   ImGuiButtonFlags_MouseButtonRight |
                                   ImGuiButtonFlags_MouseButtonMiddle);
        HandleNavigation(ImGui::IsItemHovered(), ImGui::IsItemActive(), size.y);
        DrawToolbar(origin, size);
        if (!renderError.empty()) {
            ImGui::SetCursorScreenPos(ImVec2(origin.x + 12.0f, origin.y + size.y - 28.0f));
            ImGui::TextColored(ToImVec4(theme.error), "%s", renderError.c_str());
        }
        ImGui::End();
    }

    void HandleNavigation(bool hovered, bool active, float height) {
        const ImGuiIO& io = ImGui::GetIO();
        bool moved = false;
        if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            controller.Orbit(-io.MouseDelta.x * 0.005f, io.MouseDelta.y * 0.005f);
            moved = true;
        }
        if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) ||
                       ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))) {
            controller.Pan(io.MouseDelta.x, io.MouseDelta.y, height);
            moved = true;
        }
        if (hovered && io.MouseWheel != 0.0f) {
            controller.Zoom(io.MouseWheel);
            moved = true;
        }
        if (moved &&
            (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || io.MouseWheel != 0.0f)) {
            WriteActiveCamera();
        }
        if (!io.WantTextInput && (hovered || ImGui::IsWindowFocused())) {
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
                self.Frame(selection);
            }
        }
        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            open = false;
        }
    }

    void DrawToolbar(const ImVec2& origin, const ImVec2& size) {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float pad = 8.0f * contentScale;
        ImGui::SetCursorScreenPos(ImVec2(origin.x + pad, origin.y + pad));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ToImVec4(theme.overlay));
        ImGui::PushStyleColor(ImGuiCol_Button, ToImVec4(theme.overlay));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ToImVec4(theme.panel));
        const float comboWidth = 150.0f * contentScale;

        ImGui::SetNextItemWidth(comboWidth);
        const std::string cameraLabel = std::string(icons::kCamera) + "  " + Label(activeCamera);
        if (ImGui::BeginCombo("##camera", cameraLabel.c_str())) {
            for (const Path& camera : ViewableCameras(scene)) {
                if (ImGui::Selectable(Label(camera).c_str(), camera == activeCamera)) {
                    SetActive(camera);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", camera.String().c_str());
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(comboWidth);
        const std::string varLabel = std::string(icons::kEye) + "  " + viewVar;
        if (ImGui::BeginCombo("##var", varLabel.c_str())) {
            for (const std::string_view name : kRenderVarNames) {
                const bool supported = name == "LdrColor";
                ImGui::BeginDisabled(!supported);
                if (ImGui::Selectable(std::string(name).c_str(), name == viewVar)) {
                    viewVar = name;
                }
                ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(comboWidth);
        const std::string lightLabel = std::string(icons::kSun) + "  " + LightingName(lighting);
        if (ImGui::BeginCombo("##lighting", lightLabel.c_str())) {
            for (const Lighting mode : {Lighting::Stage, Lighting::Camera, Lighting::Default}) {
                if (ImGui::Selectable(LightingName(mode), mode == lighting)) {
                    lighting = mode;
                }
            }
            ImGui::EndCombo();
        }

        char info[64];
        std::snprintf(info, sizeof(info), "%u x %u  %.0f fps", ViewportWidth(), ViewportHeight(),
                      static_cast<double>(ImGui::GetIO().Framerate));
        const float infoWidth = ImGui::CalcTextSize(info).x;
        ImGui::SetCursorScreenPos(ImVec2(origin.x + size.x - infoWidth - pad - style.FramePadding.x,
                                         origin.y + pad + style.FramePadding.y));
        ImGui::TextColored(ToImVec4(theme.textMuted), "%s", info);
        ImGui::PopStyleColor(3);
    }

    void DrawPanels() {
        for (std::size_t i = 0; i < panels.size(); ++i) {
            Panel& panel = *panels[i];
            if (!panel.IsVisible()) {
                continue;
            }
            if (sideDock != 0) {
                ImGui::SetNextWindowDockID(sideDock, ImGuiCond_FirstUseEver);
            }
            bool keepOpen = true;
            if (ImGui::Begin(panel.Title().c_str(), &keepOpen)) {
                panel.Draw(self);
            }
            ImGui::End();
            if (!keepOpen) {
                panel.SetVisible(false);
            }
        }
    }

    void BuildUi() {
        const ImGuiID dockSpace = ImGui::GetID(kDockSpace);
        if (!layoutReady) {
            BuildDefaultLayout(dockSpace);
            layoutReady = true;
        }
        DrawMenuBar();
        ImGui::DockSpaceOverViewport(dockSpace, ImGui::GetMainViewport());
        DrawViewport();
        DrawPanels();
    }

    void RecordViewport(VkCommandBuffer commands) {
        const VkClearColorValue clear{
            {theme.background.r, theme.background.g, theme.background.b, 1.0f}};
        bool drawn = false;
        try {
            UpdateSessionLights(scene, scene.WorldTransform(activeCamera));
            std::map<std::string, Value, std::less<>> overrides;
            overrides.emplace(tokens::kCameraRel, activeCamera);
            if (lighting == Lighting::Camera) {
                overrides.emplace(tokens::kLightsRel, std::vector<Path>{CameraLightPath()});
            } else if (lighting == Lighting::Default) {
                overrides.emplace(tokens::kLightsRel, std::vector<Path>{DefaultLightPath()});
            }
            MirrorSettings(scene, settingsSource, SessionSettingsPath(), overrides, mirrored);
            delegate->Update(scene, SessionSettingsPath(), viewport->Extent());
            const RenderingTarget target = viewport->Prepare(commands, clear);
            delegate->Record(commands, target);
            renderError.clear();
            drawn = true;
        } catch (const std::invalid_argument& error) {
            renderError = error.what();
        }
        if (!drawn) {
            const RenderingTarget target = viewport->Prepare(commands, clear);
            BeginRendering(commands, target);
            vkCmdEndRendering(commands);
        }
        viewport->Color().Transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void RecordUi(VkCommandBuffer commands, VkImage image, VkImageView view, VkExtent2D extent) {
        if (swapchain) {
            TransitionImage(commands, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        } else {
            uiTarget->Color().Transition(commands, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }
        RenderingTarget target;
        target.color = view;
        target.extent = extent;
        target.clearColor = {{theme.background.r, theme.background.g, theme.background.b, 1.0f}};
        BeginRendering(commands, target);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commands);
        vkCmdEndRendering(commands);
        if (swapchain) {
            TransitionImage(commands, image, VK_IMAGE_ASPECT_COLOR_BIT,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        }
    }

    void Submit(VkCommandBuffer commands, const Swapchain::Frame* image) {
        std::vector<VkSemaphoreSubmitInfo> waits;
        if (image != nullptr) {
            VkSemaphoreSubmitInfo& wait = waits.emplace_back();
            wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            wait.semaphore = image->acquired;
            wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        for (const TimelinePoint& point : delegate->SubmitWaits()) {
            VkSemaphoreSubmitInfo& wait = waits.emplace_back();
            wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            wait.semaphore = point.semaphore;
            wait.value = point.value;
            wait.stageMask = point.stages;
        }
        VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        if (image != nullptr) {
            signal.semaphore = image->rendered;
            signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        }
        VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commandInfo.commandBuffer = commands;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.waitSemaphoreInfoCount = static_cast<std::uint32_t>(waits.size());
        submit.pWaitSemaphoreInfos = waits.data();
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &commandInfo;
        submit.signalSemaphoreInfoCount = image != nullptr ? 1 : 0;
        submit.pSignalSemaphoreInfos = &signal;
        vkResetFences(gpu->Device(), 1, &frame->done);
        CheckVk(vkQueueSubmit2(gpu->Queue(), 1, &submit, frame->done), "vkQueueSubmit2");
    }

    void WriteCapture() {
        frame->Wait();
        const std::vector<std::uint8_t> texels = viewport->ReadColor();
        const VkExtent2D extent = viewport->Extent();
        const std::filesystem::path path = capture->path;
        capture.reset();
        if (stbi_write_png(path.string().c_str(), static_cast<int>(extent.width),
                           static_cast<int>(extent.height), 4, texels.data(),
                           static_cast<int>(extent.width * 4)) == 0) {
            throw std::runtime_error("Cannot write " + path.string());
        }
    }

    bool Step();
};

namespace {

void StatsPanel::Draw(Viewer& viewer) {
    const RenderStats stats = viewer.Delegate().Stats();
    ImGui::TextDisabled("Device");
    ImGui::SameLine(100.0f);
    ImGui::TextUnformatted(viewer.Context().DeviceName().c_str());
    ImGui::TextDisabled("Frame");
    ImGui::SameLine(100.0f);
    const float framerate = ImGui::GetIO().Framerate;
    ImGui::Text("%.2f ms (%.0f fps)",
                framerate > 0.0f ? 1000.0 / static_cast<double>(framerate) : 0.0,
                static_cast<double>(framerate));
    const std::uint64_t bytes = stats.gpuBytes;
    ImGui::TextDisabled("GPU memory");
    ImGui::SameLine(100.0f);
    ImGui::Text("%.1f MiB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    ImGui::TextDisabled("Triangles");
    ImGui::SameLine(100.0f);
    ImGui::Text("%llu", static_cast<unsigned long long>(stats.triangles));
    for (const auto& [name, value] : stats.counters) {
        ImGui::TextDisabled("%s", name.c_str());
        ImGui::SameLine(100.0f);
        ImGui::Text("%llu", static_cast<unsigned long long>(value));
    }
    std::vector<float> times(times_.begin(), times_.end());
    if (!times.empty() && ImPlot::BeginPlot("##frametimes", ImVec2(-1.0f, 120.0f),
                                            ImPlotFlags_NoLegend | ImPlotFlags_NoMenus)) {
        ImPlot::SetupAxes(nullptr, "ms", ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, static_cast<double>(kFrameHistory),
                                ImPlotCond_Always);
        ImPlot::PlotLine("frame", times.data(), static_cast<int>(times.size()));
        ImPlot::EndPlot();
    }
}

}  // namespace

bool Viewer::State::Step() {
    if (!open) {
        return false;
    }
    if (window != nullptr) {
        glfwPollEvents();
        if (glfwWindowShouldClose(window) != 0) {
            open = false;
            return false;
        }
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
            ImGui_ImplGlfw_Sleep(10);
            return true;
        }
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width != framebufferWidth || height != framebufferHeight) {
            ResizeSwapchain(width, height);
        }
        if (width <= 0 || height <= 0) {
            return true;
        }
    }
    frame->Wait();
    ResizeViewport();
    if (capture) {
        capture->rendered = true;
    }

    const auto now = std::chrono::steady_clock::now();
    FrameInfo info;
    info.frame = frames;
    info.time = std::chrono::duration<double>(now - start).count();
    info.delta = std::chrono::duration<double>(now - last).count();
    last = now;
    const auto callbacks = frameCallbacks;
    for (const FrameCallback& entry : callbacks) {
        entry.callback(self, info);
    }
    if (!open) {
        return false;
    }
    ValidateSession();

    std::optional<Swapchain::Frame> image;
    if (swapchain) {
        image = swapchain->Acquire(frame->acquired);
        if (!image) {
            ResizeSwapchain(framebufferWidth, framebufferHeight);
            return true;
        }
    }

    if (pendingLayout) {
        ImGui::LoadIniSettingsFromMemory(pendingLayout->c_str(), pendingLayout->size());
        pendingLayout.reset();
        layoutReady = true;
    }
    ImGui_ImplVulkan_NewFrame();
    if (window != nullptr) {
        ImGui_ImplGlfw_NewFrame();
    } else {
        ImGui::GetIO().DeltaTime = static_cast<float>(std::max(info.delta, 1e-4));
    }
    ImGui::NewFrame();
    ImGuiErrorRecoveryState recovery;
    ImGui::ErrorRecoveryStoreState(&recovery);
    try {
        BuildUi();
    } catch (...) {
        ImGuiIO& io = ImGui::GetIO();
        const bool asserting = io.ConfigErrorRecoveryEnableAssert;
        io.ConfigErrorRecoveryEnableAssert = false;
        ImGui::ErrorRecoveryTryToRecoverState(&recovery);
        io.ConfigErrorRecoveryEnableAssert = asserting;
        ImGui::EndFrame();
        throw;
    }
    ImGui::Render();
    if (stats != nullptr) {
        stats->Record(static_cast<float>(info.delta * 1000.0));
    }

    VkCommandBuffer commands = frame->commands;
    vkResetCommandBuffer(commands, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commands, &begin);
    RecordViewport(commands);
    if (image) {
        RecordUi(commands, image->image, image->view, swapchain->Extent());
    } else {
        RecordUi(commands, uiTarget->Color().Handle(), uiTarget->Color().View(),
                 uiTarget->Extent());
    }
    vkEndCommandBuffer(commands);
    Submit(commands, image ? &*image : nullptr);
    ++frames;
    if (image && !swapchain->Present(*image)) {
        ResizeSwapchain(framebufferWidth, framebufferHeight);
    }
    if (capture && capture->rendered) {
        WriteCapture();
    }
    return open;
}

Viewer::Viewer(Scene& scene, const ViewerOptions& options)
    : state_(std::make_unique<State>(*this, scene, options)) {
    State& s = *state_;
    if (options.width == 0 || options.height == 0) {
        throw std::invalid_argument("The viewer's window needs a positive size");
    }
    if (!options.hidden) {
        s.OpenWindow();
    }
    s.CreateGpu();
    s.CreateImGui();
    auto stats = std::make_unique<StatsPanel>();
    s.stats = stats.get();
    s.panels.push_back(std::move(stats));
    s.CreateSession();
}

Viewer::~Viewer() = default;

void Viewer::Run() {
    while (Step()) {
    }
}

bool Viewer::Step() {
    return state_->Step();
}

bool Viewer::IsOpen() const {
    return state_->open;
}

void Viewer::Close() {
    state_->open = false;
    if (state_->window != nullptr) {
        glfwSetWindowShouldClose(state_->window, GLFW_TRUE);
    }
}

std::uint64_t Viewer::OnFrame(std::function<void(Viewer&, const FrameInfo&)> callback) {
    const std::uint64_t id = state_->nextCallback++;
    state_->frameCallbacks.push_back({id, std::move(callback)});
    return id;
}

std::uint64_t Viewer::OnSelection(std::function<void(Viewer&, std::span<const Path>)> callback) {
    const std::uint64_t id = state_->nextCallback++;
    state_->selectionCallbacks.push_back({id, std::move(callback)});
    return id;
}

void Viewer::RemoveCallback(std::uint64_t id) {
    std::erase_if(state_->frameCallbacks, [id](const auto& entry) { return entry.id == id; });
    std::erase_if(state_->selectionCallbacks, [id](const auto& entry) { return entry.id == id; });
}

Scene& Viewer::GetScene() {
    return state_->scene;
}

RenderDelegate& Viewer::Delegate() {
    return *state_->delegate;
}

VulkanContext& Viewer::Context() {
    return *state_->gpu;
}

const VulkanContext& Viewer::Context() const {
    return *state_->gpu;
}

const Path& Viewer::SessionRoot() {
    return SessionRootPath();
}

const Path& Viewer::PerspectiveCamera() {
    static const Path path = SessionRootPath().Child("Perspective");
    return path;
}

const std::vector<Path>& Viewer::Selection() const {
    return state_->selection;
}

void Viewer::SetSelection(std::vector<Path> paths) {
    state_->SetSelection(std::move(paths));
}

std::vector<Path> Viewer::Cameras() const {
    return ViewableCameras(state_->scene);
}

const Path& Viewer::ActiveCamera() const {
    return state_->activeCamera;
}

void Viewer::SetActiveCamera(Path camera) {
    const Scene& scene = state_->scene;
    if (!scene.Has(camera) || scene.TypeOf(camera) != tokens::kCamera) {
        throw std::invalid_argument("'" + camera.String() + "' is not a Camera prim");
    }
    state_->SetActive(camera);
}

void Viewer::Frame(std::span<const Path> paths) {
    State& s = *state_;
    s.delegate->Sync(s.scene);
    std::optional<Bounds3> bounds;
    if (!paths.empty()) {
        std::vector<std::optional<Bounds3>> boxes;
        for (const Path& path : paths) {
            if (s.scene.Has(path)) {
                std::optional<Bounds3> extent = ExtentOf(s.scene, path);
                if (extent) {
                    const glm::dmat4 world = s.scene.WorldTransform(path);
                    Bounds3 box{glm::vec3(std::numeric_limits<float>::max()),
                                glm::vec3(std::numeric_limits<float>::lowest())};
                    for (int corner = 0; corner < 8; ++corner) {
                        const glm::dvec3 local((corner & 1) ? extent->second.x : extent->first.x,
                                               (corner & 2) ? extent->second.y : extent->first.y,
                                               (corner & 4) ? extent->second.z : extent->first.z);
                        const glm::vec3 point(world * glm::dvec4(local, 1.0));
                        box.first = glm::min(box.first, point);
                        box.second = glm::max(box.second, point);
                    }
                    boxes.emplace_back(box);
                }
            }
        }
        bounds = Union(boxes);
    }
    s.FrameBounds(bounds ? bounds : s.SceneBounds());
}

Path Viewer::CameraFromView(const Path& path) {
    State& s = *state_;
    if (IsSessionPath(path)) {
        throw std::invalid_argument("'" + path.String() + "' is under the viewer's session root");
    }
    if (s.scene.Has(path)) {
        throw std::invalid_argument("'" + path.String() + "' already exists");
    }
    Camera camera = CameraOf(s.scene, s.activeCamera, s.ViewportWidth(), s.ViewportHeight());
    s.scene.Define(path, tokens::kCamera);
    camera.transform = LocalFromWorld(s.scene, path, camera.transform);
    SetCamera(s.scene, path, camera);
    return path;
}

const std::string& Viewer::ViewVar() const {
    return state_->viewVar;
}

void Viewer::SetViewVar(std::string name) {
    const auto& names = kRenderVarNames;
    if (std::find(names.begin(), names.end(), name) == names.end()) {
        throw std::invalid_argument("'" + name + "' is not a render var");
    }
    if (name != "LdrColor") {
        throw std::invalid_argument("The viewport shows only LdrColor so far, not '" + name + "'");
    }
    state_->viewVar = std::move(name);
}

Lighting Viewer::GetLighting() const {
    return state_->lighting;
}

void Viewer::SetLighting(Lighting lighting) {
    state_->lighting = lighting;
}

const Path& Viewer::Settings() const {
    return state_->settingsSource;
}

void Viewer::SetSettings(Path settings) {
    const Scene& scene = state_->scene;
    if (!scene.Has(settings) || scene.TypeOf(settings) != tokens::kRenderSettings ||
        IsSessionPath(settings)) {
        throw std::invalid_argument("'" + settings.String() + "' is not a RenderSettings prim");
    }
    state_->settingsSource = std::move(settings);
}

void Viewer::Capture(const std::filesystem::path& path, std::string var) {
    if (!var.empty() && var != state_->viewVar) {
        throw std::invalid_argument("Capture supports only the shown var, " + state_->viewVar +
                                    ", so far");
    }
    state_->capture = State::CaptureRequest{path, false};
}

Panel& Viewer::AddPanel(std::unique_ptr<Panel> panel) {
    if (!panel) {
        throw std::invalid_argument("AddPanel needs a panel");
    }
    for (const auto& existing : state_->panels) {
        if (existing->Title() == panel->Title()) {
            throw std::invalid_argument("A panel titled '" + panel->Title() + "' exists");
        }
    }
    if (panel->Title() == kViewportTitle) {
        throw std::invalid_argument("'Viewport' is reserved");
    }
    state_->panels.push_back(std::move(panel));
    return *state_->panels.back();
}

WidgetPanel& Viewer::AddWidgetPanel(std::string title) {
    return static_cast<WidgetPanel&>(AddPanel(std::make_unique<WidgetPanel>(std::move(title))));
}

void Viewer::RemovePanel(const std::string& title) {
    auto& panels = state_->panels;
    const auto found = std::find_if(panels.begin(), panels.end(),
                                    [&](const auto& panel) { return panel->Title() == title; });
    if (found == panels.end()) {
        throw std::invalid_argument("No panel titled '" + title + "'");
    }
    if (found->get() == state_->stats) {
        state_->stats->SetVisible(false);
        return;
    }
    panels.erase(found);
}

void Viewer::AddMenuItem(const std::string& menu, const std::string& label,
                         std::function<void(Viewer&)> action) {
    if (menu.empty() || label.empty()) {
        throw std::invalid_argument("Menu items need a menu and a label");
    }
    state_->menuItems.push_back({menu, label, std::move(action)});
}

void Viewer::SaveLayout(const std::filesystem::path& path) const {
    std::size_t size = 0;
    const char* data = ImGui::SaveIniSettingsToMemory(&size);
    std::ofstream file(path, std::ios::binary);
    if (!file.write(data, static_cast<std::streamsize>(size))) {
        throw std::runtime_error("Cannot write " + path.string());
    }
}

void Viewer::LoadLayout(const std::filesystem::path& path) {
    state_->pendingLayout = ReadFile(path);
}

const Theme& Viewer::GetTheme() const {
    return state_->theme;
}

void Viewer::SetTheme(const Theme& theme) {
    state_->theme = theme;
    ApplyTheme(theme, state_->contentScale, ImGui::GetStyle());
}

}  // namespace ernie::gui
