#pragma once

#include <webgpu/webgpu.h>

struct GLFWwindow;

namespace terraingen::viewer {

/// Creates a WebGPU surface for a GLFW window created with `GLFW_CLIENT_API` set to
/// `GLFW_NO_API`. Supports Windows, macOS, X11 and Wayland.
/// @return The surface, or null if the window system is unsupported.
WGPUSurface CreateGlfwSurface(WGPUInstance instance, GLFWwindow* window);

}  // namespace terraingen::viewer
