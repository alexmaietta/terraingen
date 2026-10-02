#pragma once

#include "ernie/gpu/vulkan_context.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ernie {

/// Where a buffer lives and how the host reaches it.
enum class BufferMemory {
    Device,    ///< Device-local; `Write` and `Read` go through a staging copy.
    Upload,    ///< Host-visible, written sequentially by the host each frame.
    Readback,  ///< Host-visible and cached, for results the host reads.
};

/// A Vulkan buffer with its memory, allocated through VMA.
class GpuBuffer {
  public:
    /// @throws std::runtime_error if allocation fails.
    GpuBuffer(const VulkanContext& context, VkDeviceSize size, VkBufferUsageFlags usage,
              BufferMemory memory);
    ~GpuBuffer();
    GpuBuffer(GpuBuffer&& other) noexcept;
    GpuBuffer& operator=(GpuBuffer&& other) noexcept;
    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;

    VkBuffer Handle() const {
        return buffer_;
    }

    VkDeviceSize Size() const {
        return size_;
    }

    /// Copies `bytes` to the buffer at `offset`, waiting for the copy when staging is needed.
    /// @throws std::out_of_range if the range does not fit.
    void Write(std::span<const std::byte> bytes, VkDeviceSize offset = 0);

    /// Copies the buffer's contents at `offset` into `bytes`, after all submitted work that
    /// `VulkanContext::Submit` has waited for.
    /// @throws std::out_of_range if the range does not fit.
    void Read(std::span<std::byte> bytes, VkDeviceSize offset = 0) const;

  private:
    void Release();

    const VulkanContext* context_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    void* mapped_ = nullptr;
    VkDeviceSize size_ = 0;
    BufferMemory memory_ = BufferMemory::Device;
};

}  // namespace ernie
