#pragma once

#include "ernie/gpu/vulkan_context.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace ernie {

/// A presentation swapchain for a window surface, with one "rendered" semaphore per image.
///
/// The surface comes from the window system (e.g. `glfwCreateWindowSurface`) and is owned by the
/// swapchain from then on. Presentation uses FIFO (vsync) and an 8-bit UNORM format when available,
/// so colours are written to the screen unchanged, as in offscreen targets.
class Swapchain {
  public:
    /// One acquired image, ready to render into after waiting on `acquired`; signal `rendered`
    /// when done and pass the frame to `Present`.
    struct Frame {
        std::uint32_t index = 0;
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
        VkSemaphore rendered = VK_NULL_HANDLE;
    };

    /// Takes ownership of `surface` and creates images for a `width` x `height` framebuffer.
    /// @throws std::runtime_error if the device's queue cannot present to the surface or the
    /// swapchain cannot be created.
    Swapchain(const VulkanContext& context, VkSurfaceKHR surface, std::uint32_t width,
              std::uint32_t height);
    ~Swapchain();
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    /// Recreates the images for a new framebuffer size, waiting for the device to go idle first.
    void Resize(std::uint32_t width, std::uint32_t height);

    /// Acquires the next image, signalling `acquired` when it is ready; nullopt when the swapchain
    /// is out of date and must be resized.
    std::optional<Frame> Acquire(VkSemaphore acquired);

    /// Presents `frame` once `frame.rendered` is signalled; false when the swapchain is out of date
    /// or suboptimal and should be resized.
    bool Present(const Frame& frame);

    VkFormat Format() const {
        return format_;
    }

    VkExtent2D Extent() const {
        return extent_;
    }

    std::uint32_t MinImageCount() const {
        return minImageCount_;
    }

    std::uint32_t ImageCount() const {
        return static_cast<std::uint32_t>(images_.size());
    }

  private:
    void Create(std::uint32_t width, std::uint32_t height);
    void Destroy();

    const VulkanContext& context_;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    std::uint32_t minImageCount_ = 2;
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkSemaphore> rendered_;
};

}  // namespace ernie
