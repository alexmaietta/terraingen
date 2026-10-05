#include "ernie/raster/render_targets.h"

#include "ernie/core/spectrum.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace ernie {

namespace {

constexpr const auto& kVarNames = kRenderVarNames;

constexpr std::array<VkFormat, 10> kVarFormats{
    VK_FORMAT_R32_SFLOAT,        VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_FORMAT_R16G16B16A16_UINT, VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_FORMAT_R8G8B8A8_UNORM,    VK_FORMAT_R32_SFLOAT,
    VK_FORMAT_R32_SFLOAT,        VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_FORMAT_R32_UINT,          VK_FORMAT_R32_UINT};

constexpr VkImageUsageFlags kStorage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

}  // namespace

RenderTargets::RenderTargets(const VulkanContext& context, VkExtent2D extent, int bands,
                             bool shared)
    : context_(context), extent_(extent), bands_(bands), shared_(shared) {
    if (extent.width == 0 || extent.height == 0) {
        throw std::invalid_argument("Render targets need a non-zero extent");
    }
    SpectralBands{380.0, 780.0, bands}.Check();
    for (std::size_t i = 0; i < gbuffer_.size(); ++i) {
        gbuffer_[i] = std::make_unique<GpuImage>(
            context, ImageDesc{extent.width, extent.height, 1, 1, kGBufferFormats[i],
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});
    }
    depth_ = std::make_unique<GpuImage>(
        context,
        ImageDesc{extent.width, extent.height, 1, 1, kDepthFormat,
                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});
    const auto layers = static_cast<std::uint32_t>(bands);
    rasterRadiance_ =
        std::make_unique<GpuImage>(context, ImageDesc{extent.width, extent.height, layers, 1,
                                                      VK_FORMAT_R32_SFLOAT, kStorage, false, true});
    for (std::size_t i = 0; i < kVarNames.size(); ++i) {
        const bool spectral = kVarNames[i] == render_vars::kSpectralRadiance;
        vars_.push_back(std::make_unique<GpuImage>(
            context, ImageDesc{extent.width, extent.height, spectral ? layers : 1u, 1,
                               kVarFormats[i], kStorage, shared && !spectral, spectral}));
    }
}

RenderingTarget RenderTargets::PrepareGBuffer(VkCommandBuffer commands) {
    RenderingTarget target;
    for (const std::unique_ptr<GpuImage>& image : gbuffer_) {
        image->Transition(commands, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        target.colors.push_back(image->View());
    }
    VkClearColorValue background{};
    background.uint32[0] = 0xFFFFFFFFu;
    target.clearColors = {VkClearColorValue{}, VkClearColorValue{}, background};
    depth_->Transition(commands, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    target.depth = depth_->View();
    target.extent = extent_;
    return target;
}

void RenderTargets::PrepareCompute(VkCommandBuffer commands) {
    for (const std::unique_ptr<GpuImage>& image : gbuffer_) {
        image->Transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    depth_->Transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    rasterRadiance_->Transition(commands, VK_IMAGE_LAYOUT_GENERAL);
    for (const std::unique_ptr<GpuImage>& image : vars_) {
        image->Transition(commands, VK_IMAGE_LAYOUT_GENERAL);
    }
}

GpuImage* RenderTargets::Var(std::string_view name) {
    for (std::size_t i = 0; i < kVarNames.size(); ++i) {
        if (kVarNames[i] == name) {
            return vars_[i].get();
        }
    }
    return nullptr;
}

std::span<const std::string_view> RenderTargets::VarNames() {
    return kVarNames;
}

std::uint64_t RenderTargets::GpuBytes() const {
    std::uint64_t bytes = depth_->Bytes() + rasterRadiance_->Bytes();
    for (const std::unique_ptr<GpuImage>& image : gbuffer_) {
        bytes += image->Bytes();
    }
    for (const std::unique_ptr<GpuImage>& image : vars_) {
        bytes += image->Bytes();
    }
    return bytes;
}

}  // namespace ernie
