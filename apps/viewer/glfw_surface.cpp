#include "glfw_surface.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdint>
#include <stdexcept>

namespace terraingen::viewer {

std::vector<std::string> GlfwInstanceExtensions() {
    std::uint32_t count = 0;
    const char** names = glfwGetRequiredInstanceExtensions(&count);
    if (names == nullptr) {
        throw std::runtime_error("GLFW cannot present with Vulkan on this system");
    }
    return {names, names + count};
}

VkSurfaceKHR CreateGlfwSurface(VkInstance instance, GLFWwindow* window) {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult result = glfwCreateWindowSurface(instance, window, nullptr, &surface);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Cannot create a Vulkan surface for the window (VkResult " +
                                 std::to_string(static_cast<int>(result)) + ")");
    }
    return surface;
}

}  // namespace terraingen::viewer
