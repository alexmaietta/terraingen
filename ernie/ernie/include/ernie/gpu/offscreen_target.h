#pragma once

#include "ernie/gpu/gpu_image.h"
#include "ernie/gpu/pipeline.h"
#include "ernie/gpu/vulkan_context.h"

#include <cstdint>
#include <vector>

namespace ernie {

/// A colour and depth target for headless rendering, with readback of the colour image.
class OffscreenTarget {
  public:
    /// @throws std::runtime_error if the images cannot be created.
    OffscreenTarget(const VulkanContext& context, std::uint32_t width, std::uint32_t height,
                    VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
                    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT);

    VkExtent2D Extent() const {
        return {color_.Desc().width, color_.Desc().height};
    }

    VkFormat ColorFormat() const {
        return color_.Desc().format;
    }

    VkFormat DepthFormat() const {
        return depth_.Desc().format;
    }

    /// Records the transitions that make both images attachments, and returns the attachments for
    /// `BeginRendering`, clearing to `clear`.
    RenderingTarget Prepare(VkCommandBuffer commands, VkClearColorValue clear = {});

    /// Colour texels, rows top to bottom, tightly packed; for 8-bit formats four bytes per texel.
    /// Waits for submitted work through `VulkanContext::Submit`.
    std::vector<std::uint8_t> ReadColor();

  private:
    const VulkanContext& context_;
    GpuImage color_;
    GpuImage depth_;
};

}  // namespace ernie
