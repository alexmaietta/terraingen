#include "ernie/raster/render_targets.h"

#include "ernie/core/spectrum.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace ernie {

namespace {

constexpr std::array<std::string_view, 10> kVarNames{
    render_vars::kSpectralRadiance, render_vars::kElectrons, render_vars::kRawDn,
    render_vars::kHdrColor,         render_vars::kLdrColor,  render_vars::kDistanceToImagePlane,
    render_vars::kDistanceToCamera, render_vars::kNormal,    render_vars::kPrimId,
    render_vars::kInstanceId};

constexpr std::array<VkFormat, 10> kVarFormats{
    VK_FORMAT_R32_SFLOAT,        VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_FORMAT_R16G16B16A16_UINT, VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_FORMAT_R8G8B8A8_UNORM,    VK_FORMAT_R32_SFLOAT,
    VK_FORMAT_R32_SFLOAT,        VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_FORMAT_R32_UINT,          VK_FORMAT_R32_UINT};

constexpr VkImageUsageFlags kStorage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

}  // namespace

std::string_view NameOf(SensorFilter filter) {
    switch (filter) {
    case SensorFilter::Lens:
        return "lens";
    case SensorFilter::Qe:
        return "qe";
    case SensorFilter::Noise:
        return "noise";
    case SensorFilter::Adc:
        return "adc";
    case SensorFilter::Cie:
        return "cie";
    case SensorFilter::Exposure:
        return "exposure";
    }
    return "";
}

std::optional<SensorFilter> SensorFilterNamed(std::string_view name) {
    for (const SensorFilter filter : kSensorFilters) {
        if (NameOf(filter) == name) {
            return filter;
        }
    }
    return std::nullopt;
}

std::vector<SensorFilter> DefaultFilterChain() {
    return {kSensorFilters.begin(), kSensorFilters.end()};
}

std::vector<SensorFilter> CheckFilterChain(std::span<const SensorFilter> chain) {
    std::vector<SensorFilter> checked;
    if (chain.empty() || chain.front() != SensorFilter::Lens) {
        checked.push_back(SensorFilter::Lens);
    }
    const auto seen = [&checked](SensorFilter filter) {
        return std::find(checked.begin(), checked.end(), filter) != checked.end();
    };
    for (const SensorFilter filter : chain) {
        if (seen(filter)) {
            throw std::invalid_argument("Sensor filter '" + std::string(NameOf(filter)) +
                                        "' appears twice");
        }
        if ((filter == SensorFilter::Noise || filter == SensorFilter::Adc) &&
            !seen(SensorFilter::Qe)) {
            throw std::invalid_argument("Sensor filter '" + std::string(NameOf(filter)) +
                                        "' needs 'qe' before it");
        }
        if (filter == SensorFilter::Exposure && !seen(SensorFilter::Cie)) {
            throw std::invalid_argument("Sensor filter 'exposure' needs 'cie' before it");
        }
        checked.push_back(filter);
    }
    return checked;
}

std::vector<std::string_view> VarsWrittenBy(std::span<const SensorFilter> chain) {
    std::vector<std::string_view> vars;
    for (const SensorFilter filter : chain) {
        switch (filter) {
        case SensorFilter::Lens:
            vars.insert(vars.end(),
                        {render_vars::kSpectralRadiance, render_vars::kDistanceToImagePlane,
                         render_vars::kDistanceToCamera, render_vars::kNormal, render_vars::kPrimId,
                         render_vars::kInstanceId});
            break;
        case SensorFilter::Qe:
            vars.push_back(render_vars::kElectrons);
            break;
        case SensorFilter::Noise:
            break;
        case SensorFilter::Adc:
            vars.push_back(render_vars::kRawDn);
            break;
        case SensorFilter::Cie:
            vars.push_back(render_vars::kHdrColor);
            break;
        case SensorFilter::Exposure:
            vars.push_back(render_vars::kLdrColor);
            break;
        }
    }
    return vars;
}

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
