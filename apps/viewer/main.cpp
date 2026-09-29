#include "render/glfw_surface.h"
#include "render/gpu_context.h"
#include "terraingen/version.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_wgpu.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>

namespace {

struct Options {
    long maxFrames = -1;
};

Options ParseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            options.maxFrames = std::strtol(argv[++i], nullptr, 10);
        }
    }
    return options;
}

void DrawPanel(const terraingen::render::GpuContext& gpu, ImVec4& clearColor) {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("TerrainGen");
    ImGui::Text("TerrainGen %s", TERRAINGEN_VERSION_STRING);
    ImGui::Separator();
    ImGui::Text("GPU: %s", gpu.AdapterDescription().c_str());
    ImGui::Text("Framebuffer: %.0f x %.0f", static_cast<double>(io.DisplaySize.x),
                static_cast<double>(io.DisplaySize.y));
    ImGui::Text("%.2f ms/frame (%.0f FPS)", 1000.0 / static_cast<double>(io.Framerate),
                static_cast<double>(io.Framerate));
    ImGui::Separator();
    ImGui::TextDisabled("No terrain loaded");
    ImGui::ColorEdit3("Background", &clearColor.x);
    ImGui::End();
}

void RenderFrame(const terraingen::render::GpuContext& gpu, WGPUTexture target,
                 const ImVec4& clearColor) {
    WGPUTextureViewDescriptor viewDescriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    viewDescriptor.format = gpu.SurfaceFormat();
    viewDescriptor.dimension = WGPUTextureViewDimension_2D;
    viewDescriptor.mipLevelCount = 1;
    viewDescriptor.arrayLayerCount = 1;
    WGPUTextureView view = wgpuTextureCreateView(target, &viewDescriptor);

    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = view;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = {static_cast<double>(clearColor.x), static_cast<double>(clearColor.y),
                        static_cast<double>(clearColor.z), 1.0};

    WGPURenderPassDescriptor passDescriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDescriptor.colorAttachmentCount = 1;
    passDescriptor.colorAttachments = &color;

    WGPUCommandEncoderDescriptor encoderDescriptor = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu.Device(), &encoderDescriptor);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDescriptor);
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass);
    wgpuRenderPassEncoderEnd(pass);

    WGPUCommandBufferDescriptor commandsDescriptor = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, &commandsDescriptor);
    wgpuQueueSubmit(gpu.Queue(), 1, &commands);

    wgpuCommandBufferRelease(commands);
    wgpuRenderPassEncoderRelease(pass);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(view);
}

int Run(const Options& options) {
    glfwSetErrorCallback([](int code, const char* message) {
        std::fprintf(stderr, "GLFW %d: %s\n", code, message);
    });
    if (glfwInit() == GLFW_FALSE) {
        return EXIT_FAILURE;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 800, "TerrainGen Viewer", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    auto gpu = terraingen::render::GpuContext::Create([window](WGPUInstance instance) {
        return terraingen::render::CreateGlfwSurface(instance, window);
    });

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    gpu->ConfigureSurface(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOther(window, true);
    ImGui_ImplWGPU_InitInfo imguiInfo;
    imguiInfo.Device = gpu->Device();
    imguiInfo.RenderTargetFormat = gpu->SurfaceFormat();
    ImGui_ImplWGPU_Init(&imguiInfo);

    ImVec4 clearColor(0.08f, 0.08f, 0.10f, 1.0f);
    for (long frame = 0; !glfwWindowShouldClose(window) && frame != options.maxFrames; ++frame) {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        int newWidth = 0;
        int newHeight = 0;
        glfwGetFramebufferSize(window, &newWidth, &newHeight);
        if (newWidth != width || newHeight != height) {
            width = newWidth;
            height = newHeight;
            gpu->ConfigureSurface(static_cast<std::uint32_t>(width),
                                  static_cast<std::uint32_t>(height));
        }

        WGPUSurfaceTexture surfaceTexture = WGPU_SURFACE_TEXTURE_INIT;
        wgpuSurfaceGetCurrentTexture(gpu->Surface(), &surfaceTexture);
        if (ImGui_ImplWGPU_IsSurfaceStatusError(surfaceTexture.status)) {
            std::fprintf(stderr, "Unrecoverable surface status %#x\n",
                         static_cast<unsigned>(surfaceTexture.status));
            break;
        }
        if (ImGui_ImplWGPU_IsSurfaceStatusSubOptimal(surfaceTexture.status)) {
            if (surfaceTexture.texture != nullptr) {
                wgpuTextureRelease(surfaceTexture.texture);
            }
            if (width > 0 && height > 0) {
                gpu->ConfigureSurface(static_cast<std::uint32_t>(width),
                                      static_cast<std::uint32_t>(height));
            }
            continue;
        }

        ImGui_ImplWGPU_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        DrawPanel(*gpu, clearColor);
        ImGui::Render();

        RenderFrame(*gpu, surfaceTexture.texture, clearColor);
        wgpuSurfacePresent(gpu->Surface());
        wgpuTextureRelease(surfaceTexture.texture);
    }

    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    gpu.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return Run(ParseOptions(argc, argv));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "terraingen-viewer: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
