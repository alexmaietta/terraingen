#include "ernie/gpu/gpu_image.h"

#include "ernie/gpu/gpu_buffer.h"
#include "ernie/gpu/pipeline.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ernie {

VkImageAspectFlags AspectOf(VkFormat format) {
    switch (format) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    default:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

std::uint32_t GpuImage::TexelBytes(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_R16G16_SFLOAT:
    case VK_FORMAT_R32_UINT:
    case VK_FORMAT_D32_SFLOAT:
        return 4;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_R32G32_SFLOAT:
        return 8;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return 16;
    default:
        throw std::invalid_argument("Unsupported texel format");
    }
}

GpuImage::GpuImage(const VulkanContext& context, const ImageDesc& desc)
    : context_(&context), desc_(desc) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = desc.format;
    info.extent = {desc.width, desc.height, 1};
    info.mipLevels = desc.mips;
    info.arrayLayers = desc.layers;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = desc.usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (desc.shared) {
        if (!context.SupportsExternalMemory()) {
            throw std::runtime_error("The Vulkan device cannot export image memory");
        }
        VkExternalMemoryImageCreateInfo external{
            VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
        external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        info.pNext = &external;
        CheckVk(vkCreateImage(context.Device(), &info, nullptr, &image_), "vkCreateImage");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(context.Device(), image_, &requirements);
        VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
        dedicated.image = image_;
        VkExportMemoryAllocateInfo exported{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
        exported.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        exported.pNext = &dedicated;
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocate.pNext = &exported;
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = context.DeviceLocalMemoryType(requirements.memoryTypeBits);
        const VkResult result =
            vkAllocateMemory(context.Device(), &allocate, nullptr, &sharedMemory_);
        if (result != VK_SUCCESS) {
            vkDestroyImage(context.Device(), image_, nullptr);
            image_ = VK_NULL_HANDLE;
            CheckVk(result, "vkAllocateMemory");
        }
        CheckVk(vkBindImageMemory(context.Device(), image_, sharedMemory_, 0), "vkBindImageMemory");
        bytes_ = requirements.size;
    } else {
        VmaAllocationCreateInfo allocation{};
        allocation.usage = VMA_MEMORY_USAGE_AUTO;
        VmaAllocationInfo result{};
        CheckVk(
            vmaCreateImage(context.Allocator(), &info, &allocation, &image_, &allocation_, &result),
            "vmaCreateImage");
        bytes_ = result.size;
    }

    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = image_;
    view.viewType = desc.layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    view.format = desc.format;
    view.subresourceRange = {AspectOf(desc.format), 0, desc.mips, 0, desc.layers};
    CheckVk(vkCreateImageView(context.Device(), &view, nullptr, &view_), "vkCreateImageView");
    if (desc.layers > 1) {
        for (std::uint32_t layer = 0; layer < desc.layers; ++layer) {
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.subresourceRange = {AspectOf(desc.format), 0, 1, layer, 1};
            VkImageView layerView = VK_NULL_HANDLE;
            CheckVk(vkCreateImageView(context.Device(), &view, nullptr, &layerView),
                    "vkCreateImageView");
            layerViews_.push_back(layerView);
        }
    }
}

GpuImage::~GpuImage() {
    Release();
}

GpuImage::GpuImage(GpuImage&& other) noexcept
    : context_(other.context_), desc_(other.desc_),
      image_(std::exchange(other.image_, VK_NULL_HANDLE)),
      allocation_(std::exchange(other.allocation_, VK_NULL_HANDLE)),
      sharedMemory_(std::exchange(other.sharedMemory_, VK_NULL_HANDLE)),
      attached_(std::move(other.attached_)), view_(std::exchange(other.view_, VK_NULL_HANDLE)),
      layerViews_(std::move(other.layerViews_)), layout_(other.layout_), bytes_(other.bytes_) {}

GpuImage& GpuImage::operator=(GpuImage&& other) noexcept {
    if (this != &other) {
        Release();
        context_ = other.context_;
        desc_ = other.desc_;
        image_ = std::exchange(other.image_, VK_NULL_HANDLE);
        allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
        sharedMemory_ = std::exchange(other.sharedMemory_, VK_NULL_HANDLE);
        attached_ = std::move(other.attached_);
        view_ = std::exchange(other.view_, VK_NULL_HANDLE);
        layerViews_ = std::move(other.layerViews_);
        layout_ = other.layout_;
        bytes_ = other.bytes_;
    }
    return *this;
}

void GpuImage::Release() {
    if (image_ == VK_NULL_HANDLE) {
        return;
    }
    for (VkImageView view : layerViews_) {
        vkDestroyImageView(context_->Device(), view, nullptr);
    }
    layerViews_.clear();
    attached_.reset();
    vkDestroyImageView(context_->Device(), view_, nullptr);
    if (sharedMemory_ != VK_NULL_HANDLE) {
        vkDestroyImage(context_->Device(), image_, nullptr);
        vkFreeMemory(context_->Device(), sharedMemory_, nullptr);
        sharedMemory_ = VK_NULL_HANDLE;
    } else {
        vmaDestroyImage(context_->Allocator(), image_, allocation_);
    }
    image_ = VK_NULL_HANDLE;
    view_ = VK_NULL_HANDLE;
}

int GpuImage::ExportFd() const {
    if (sharedMemory_ == VK_NULL_HANDLE) {
        throw std::logic_error("Only shared images export their memory");
    }
    VkMemoryGetFdInfoKHR info{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
    info.memory = sharedMemory_;
    info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    int fd = -1;
    CheckVk(context_->GetMemoryFd()(context_->Device(), &info, &fd), "vkGetMemoryFdKHR");
    return fd;
}

VkImageView GpuImage::LayerView(std::uint32_t layer) const {
    if (layer >= desc_.layers) {
        throw std::out_of_range("GpuImage::LayerView past the last layer");
    }
    return desc_.layers > 1 ? layerViews_[layer] : view_;
}

void GpuImage::Transition(VkCommandBuffer commands, VkImageLayout layout) {
    TransitionImage(commands, image_, AspectOf(desc_.format), layout_, layout);
    layout_ = layout;
}

void GpuImage::Upload(std::span<const std::span<const std::byte>> mipData) {
    if (mipData.size() != desc_.mips) {
        throw std::invalid_argument("GpuImage::Upload needs data for every mip");
    }
    const std::uint32_t texel = TexelBytes(desc_.format);
    std::vector<VkBufferImageCopy> regions;
    VkDeviceSize total = 0;
    for (std::uint32_t mip = 0; mip < desc_.mips; ++mip) {
        const std::uint32_t w = std::max(1u, desc_.width >> mip);
        const std::uint32_t h = std::max(1u, desc_.height >> mip);
        if (mipData[mip].size() != static_cast<std::size_t>(w) * h * texel) {
            throw std::invalid_argument("GpuImage::Upload got the wrong size for a mip");
        }
        VkBufferImageCopy region{};
        region.bufferOffset = total;
        region.imageSubresource = {AspectOf(desc_.format), mip, 0, 1};
        region.imageExtent = {w, h, 1};
        regions.push_back(region);
        total += mipData[mip].size();
    }
    GpuBuffer staging(*context_, total, 0, BufferMemory::Upload);
    for (std::uint32_t mip = 0; mip < desc_.mips; ++mip) {
        staging.Write(mipData[mip], regions[mip].bufferOffset);
    }
    context_->Submit([&](VkCommandBuffer commands) {
        Transition(commands, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdCopyBufferToImage(commands, staging.Handle(), image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<std::uint32_t>(regions.size()), regions.data());
        Transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });
}

Sampler::Sampler(const VulkanContext& context, const SamplerDesc& desc) : context_(context) {
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = desc.filter;
    info.minFilter = desc.filter;
    info.mipmapMode = desc.mipmapMode;
    info.addressModeU = desc.addressMode;
    info.addressModeV = desc.addressMode;
    info.addressModeW = desc.addressMode;
    info.maxLod = VK_LOD_CLAMP_NONE;
    const float limit = context.Limits().maxSamplerAnisotropy;
    if (desc.maxAnisotropy > 1.0f && context.SupportsAnisotropy() && limit > 1.0f) {
        info.anisotropyEnable = VK_TRUE;
        info.maxAnisotropy = std::min(desc.maxAnisotropy, limit);
    }
    if (desc.compare) {
        info.compareEnable = VK_TRUE;
        info.compareOp = VK_COMPARE_OP_LESS;
    }
    CheckVk(vkCreateSampler(context.Device(), &info, nullptr, &sampler_), "vkCreateSampler");
}

Sampler::~Sampler() {
    vkDestroySampler(context_.Device(), sampler_, nullptr);
}

}  // namespace ernie
