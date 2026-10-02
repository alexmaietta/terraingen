#include "ernie/gpu/offscreen_target.h"

#include "ernie/gpu/gpu_buffer.h"

#include <cstddef>
#include <span>

namespace ernie {

OffscreenTarget::OffscreenTarget(const VulkanContext& context, std::uint32_t width,
                                 std::uint32_t height, VkFormat colorFormat, VkFormat depthFormat)
    : context_(context),
      color_(context, {width, height, 1, 1, colorFormat,
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT}),
      depth_(context,
             {width, height, 1, 1, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT}) {}

RenderingTarget OffscreenTarget::Prepare(VkCommandBuffer commands, VkClearColorValue clear) {
    color_.Transition(commands, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    depth_.Transition(commands, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    RenderingTarget target;
    target.color = color_.View();
    target.depth = depth_.View();
    target.extent = Extent();
    target.clearColor = clear;
    return target;
}

std::vector<std::uint8_t> OffscreenTarget::ReadColor() {
    const VkExtent2D extent = Extent();
    const std::size_t bytes = static_cast<std::size_t>(extent.width) * extent.height *
                              GpuImage::TexelBytes(ColorFormat());
    GpuBuffer readback(context_, bytes, 0, BufferMemory::Readback);
    context_.Submit([&](VkCommandBuffer commands) {
        color_.Transition(commands, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {extent.width, extent.height, 1};
        vkCmdCopyImageToBuffer(commands, color_.Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readback.Handle(), 1, &region);
    });
    std::vector<std::uint8_t> texels(bytes);
    readback.Read(std::as_writable_bytes(std::span(texels)));
    return texels;
}

}  // namespace ernie
