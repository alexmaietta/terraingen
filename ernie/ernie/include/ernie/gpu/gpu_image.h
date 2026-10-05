#pragma once

#include "ernie/gpu/vulkan_context.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace ernie {

/// Description of a 2D image or 2D image array.
struct ImageDesc {
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    std::uint32_t layers = 1;
    std::uint32_t mips = 1;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    /// Allocate in a dedicated allocation exportable as a POSIX file descriptor, for CUDA to read
    /// the image in place (`VulkanContext::SupportsExternalMemory`).
    bool shared = false;
    /// View the image as a 2D array even with one layer, for shaders that declare an array.
    bool array = false;
};

/// A device-local Vulkan image with a view of all its mips and layers, allocated through VMA.
///
/// The image tracks one layout for all its subresources; `Transition` records the barrier that
/// moves it to a new one.
class GpuImage {
  public:
    /// @throws std::runtime_error if allocation fails.
    GpuImage(const VulkanContext& context, const ImageDesc& desc);
    ~GpuImage();
    GpuImage(GpuImage&& other) noexcept;
    GpuImage& operator=(GpuImage&& other) noexcept;
    GpuImage(const GpuImage&) = delete;
    GpuImage& operator=(const GpuImage&) = delete;

    VkImage Handle() const {
        return image_;
    }

    /// View of every mip and layer (`VK_IMAGE_VIEW_TYPE_2D_ARRAY` when there are several layers).
    VkImageView View() const {
        return view_;
    }

    /// View of one layer's first mip, e.g. to render into one cascade of a shadow map.
    /// @throws std::out_of_range for a layer the image does not have.
    VkImageView LayerView(std::uint32_t layer) const;

    const ImageDesc& Desc() const {
        return desc_;
    }

    VkImageLayout Layout() const {
        return layout_;
    }

    /// Bytes of device memory the image occupies.
    VkDeviceSize Bytes() const {
        return bytes_;
    }

    /// Records a barrier moving every subresource to `layout`; waits for and makes visible all
    /// earlier work, which is simple rather than minimal.
    void Transition(VkCommandBuffer commands, VkImageLayout layout);

    /// Uploads tightly packed texels for each mip of layer 0 (`mipData[m]` holds mip `m`) and
    /// leaves the image ready for sampling in fragment and compute shaders.
    /// @throws std::invalid_argument if the number of mips or a mip's size does not match.
    void Upload(std::span<const std::span<const std::byte>> mipData);

    /// For shared images, a new file descriptor for the image's memory, owned by the caller until
    /// it imports it elsewhere (CUDA takes ownership on import).
    /// @throws std::logic_error for other images, or std::runtime_error if export fails.
    int ExportFd() const;

    /// For shared images, the size of the image's dedicated memory allocation.
    VkDeviceSize AllocationSize() const {
        return sharedMemory_ != VK_NULL_HANDLE ? bytes_ : 0;
    }

    /// Keeps `object`, such as a CUDA import of the image's memory, until the image is destroyed,
    /// releasing it before the memory is freed.
    void Attach(std::shared_ptr<void> object) {
        attached_ = std::move(object);
    }

    const std::shared_ptr<void>& Attached() const {
        return attached_;
    }

    /// Bytes per texel of the formats Ernie uploads and reads back.
    /// @throws std::invalid_argument for other formats.
    static std::uint32_t TexelBytes(VkFormat format);

  private:
    void Release();

    const VulkanContext* context_ = nullptr;
    ImageDesc desc_;
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkDeviceMemory sharedMemory_ = VK_NULL_HANDLE;
    std::shared_ptr<void> attached_;
    VkImageView view_ = VK_NULL_HANDLE;
    std::vector<VkImageView> layerViews_;
    VkImageLayout layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    VkDeviceSize bytes_ = 0;
};

/// Aspect of an image format: depth for depth formats, colour otherwise.
VkImageAspectFlags AspectOf(VkFormat format);

/// Sampler filtering and addressing.
struct SamplerDesc {
    VkFilter filter = VK_FILTER_LINEAR;
    VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    float maxAnisotropy = 1.0f;  ///< Above 1 enables anisotropic filtering where supported.
    bool compare = false;        ///< Depth comparison (`VK_COMPARE_OP_LESS`) for shadow maps.
};

/// An owned `VkSampler`.
class Sampler {
  public:
    Sampler(const VulkanContext& context, const SamplerDesc& desc);
    ~Sampler();
    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    VkSampler Handle() const {
        return sampler_;
    }

  private:
    const VulkanContext& context_;
    VkSampler sampler_ = VK_NULL_HANDLE;
};

}  // namespace ernie
