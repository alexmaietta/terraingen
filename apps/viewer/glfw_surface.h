#pragma once

#include <volk.h>

#include <string>
#include <vector>

struct GLFWwindow;

namespace terraingen::viewer {

/// Instance extensions GLFW needs to present to its windows.
/// @throws std::runtime_error if GLFW reports that Vulkan is unavailable.
std::vector<std::string> GlfwInstanceExtensions();

/// Creates a Vulkan surface for `window`; the caller owns it.
/// @throws std::runtime_error if GLFW cannot create the surface.
VkSurfaceKHR CreateGlfwSurface(VkInstance instance, GLFWwindow* window);

}  // namespace terraingen::viewer
