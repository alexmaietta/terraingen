#include "ernie/gpu/offscreen_target.h"

#include "ernie/gpu/gpu_buffer.h"

#include <cstddef>
#include <span>
#include <stdexcept>

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

VkDeviceSize OffscreenTarget::ColorBytes() const {
    const VkExtent2D extent = Extent();
    return VkDeviceSize{extent.width} * extent.height * GpuImage::TexelBytes(ColorFormat());
}

void OffscreenTarget::RecordCopyColor(VkCommandBuffer commands, GpuBuffer& buffer) {
    if (buffer.Size() < ColorBytes()) {
        throw std::invalid_argument("OffscreenTarget::CopyColor into a buffer that is too small");
    }
    const VkExtent2D extent = Extent();
    color_.Transition(commands, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(commands, color_.Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           buffer.Handle(), 1, &region);
}

void OffscreenTarget::CopyColor(GpuBuffer& buffer) {
    if (buffer.Size() < ColorBytes()) {
        throw std::invalid_argument("OffscreenTarget::CopyColor into a buffer that is too small");
    }
    context_.Submit([&](VkCommandBuffer commands) { RecordCopyColor(commands, buffer); });
}

std::vector<std::uint8_t> OffscreenTarget::ReadColor() {
    const auto bytes = static_cast<std::size_t>(ColorBytes());
    GpuBuffer readback(context_, bytes, 0, BufferMemory::Readback);
    CopyColor(readback);
    std::vector<std::uint8_t> texels(bytes);
    readback.Read(std::as_writable_bytes(std::span(texels)));
    return texels;
}

}  // namespace ernie
