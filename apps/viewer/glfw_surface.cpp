#include "glfw_surface.h"

#include <imgui_impl_wgpu.h>

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#else
#define GLFW_EXPOSE_NATIVE_X11
#if defined(__has_include) && __has_include(<wayland-client.h>)
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace terraingen::viewer {

WGPUSurface CreateGlfwSurface(WGPUInstance instance, GLFWwindow* window) {
    ImGui_ImplWGPU_CreateSurfaceInfo info = {};
    info.Instance = instance;
#if defined(_WIN32)
    info.System = "win32";
    info.RawWindow = static_cast<void*>(glfwGetWin32Window(window));
    info.RawInstance = static_cast<void*>(::GetModuleHandle(nullptr));
    return ImGui_ImplWGPU_CreateWGPUSurfaceHelper(&info);
#elif defined(__APPLE__)
    info.System = "cocoa";
    info.RawWindow = static_cast<void*>(glfwGetCocoaWindow(window));
    return ImGui_ImplWGPU_CreateWGPUSurfaceHelper(&info);
#else
#if defined(GLFW_EXPOSE_NATIVE_WAYLAND)
    if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
        info.System = "wayland";
        info.RawDisplay = static_cast<void*>(glfwGetWaylandDisplay());
        info.RawSurface = static_cast<void*>(glfwGetWaylandWindow(window));
        return ImGui_ImplWGPU_CreateWGPUSurfaceHelper(&info);
    }
#endif
    if (glfwGetPlatform() == GLFW_PLATFORM_X11) {
        info.System = "x11";
        info.RawDisplay = static_cast<void*>(glfwGetX11Display());
        info.RawWindow = reinterpret_cast<void*>(glfwGetX11Window(window));
        return ImGui_ImplWGPU_CreateWGPUSurfaceHelper(&info);
    }
    return nullptr;
#endif
}

}  // namespace terraingen::viewer
