#pragma once

#include "ernie/camera/sensor_filters.h"
#include "ernie/gpu/gpu_image.h"
#include "ernie/gpu/pipeline.h"
#include "ernie/gpu/vulkan_context.h"
#include "ernie/render/render_outputs.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ernie {

/// The images one frame renders into: the G-buffer and depth of the raster passes, the spectral
/// radiance of the shading pass, and every render var the sensor filters write. Render var images
/// stay in the general layout between frames; with `shared` set the single-layer ones live in
/// memory CUDA can map in place.
class RenderTargets : public FrameImages {
  public:
    /// G-buffer attachment formats: normal and visibility, albedo scale and modulation, ids.
    static constexpr std::array<VkFormat, 3> kGBufferFormats{
        VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32G32B32A32_UINT};
    static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

    /// @throws std::runtime_error if allocation fails, or std::invalid_argument for a zero extent
    /// or a band count outside 1 to 64.
    RenderTargets(const VulkanContext& context, VkExtent2D extent, int bands, bool shared);

    VkExtent2D Extent() const override {
        return extent_;
    }

    int Bands() const {
        return bands_;
    }

    bool Shared() const {
        return shared_;
    }

    /// Records the transitions that make the G-buffer and depth attachments, and returns them
    /// for `BeginRendering`, clearing the ids to "background".
    RenderingTarget PrepareGBuffer(VkCommandBuffer commands);

    /// Records the transitions that let compute passes read the G-buffer, depth and raster
    /// radiance and write the radiance and the render vars.
    void PrepareCompute(VkCommandBuffer commands);

    GpuImage& GBuffer(std::size_t index) {
        return *gbuffer_.at(index);
    }

    GpuImage& Depth() {
        return *depth_;
    }

    /// Spectral radiance of the raster image, before the lens filter.
    GpuImage& RasterRadiance() {
        return *rasterRadiance_;
    }

    /// The image of render var `name`, or null for an unknown name.
    GpuImage* Var(std::string_view name) override;

    /// Every render var name, in binding order of the filters.
    static std::span<const std::string_view> VarNames();

    std::uint64_t GpuBytes() const;

  private:
    const VulkanContext& context_;
    VkExtent2D extent_;
    int bands_;
    bool shared_;
    std::array<std::unique_ptr<GpuImage>, 3> gbuffer_;
    std::unique_ptr<GpuImage> depth_;
    std::unique_ptr<GpuImage> rasterRadiance_;
    std::vector<std::unique_ptr<GpuImage>> vars_;
};

}  // namespace ernie
