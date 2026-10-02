#include "ernie/gpu/gpu_buffer.h"

#include <cstring>
#include <stdexcept>
#include <utility>

namespace ernie {

GpuBuffer::GpuBuffer(const VulkanContext& context, VkDeviceSize size, VkBufferUsageFlags usage,
                     BufferMemory memory)
    : context_(&context), size_(size), memory_(memory) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO;
    if (memory == BufferMemory::Upload) {
        allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT;
    } else if (memory == BufferMemory::Readback) {
        allocation.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    VmaAllocationInfo result{};
    CheckVk(
        vmaCreateBuffer(context.Allocator(), &info, &allocation, &buffer_, &allocation_, &result),
        "vmaCreateBuffer");
    mapped_ = result.pMappedData;
}

GpuBuffer::~GpuBuffer() {
    Release();
}

GpuBuffer::GpuBuffer(GpuBuffer&& other) noexcept
    : context_(other.context_), buffer_(std::exchange(other.buffer_, VK_NULL_HANDLE)),
      allocation_(std::exchange(other.allocation_, VK_NULL_HANDLE)),
      mapped_(std::exchange(other.mapped_, nullptr)), size_(std::exchange(other.size_, 0)),
      memory_(other.memory_) {}

GpuBuffer& GpuBuffer::operator=(GpuBuffer&& other) noexcept {
    if (this != &other) {
        Release();
        context_ = other.context_;
        buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
        allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
        mapped_ = std::exchange(other.mapped_, nullptr);
        size_ = std::exchange(other.size_, 0);
        memory_ = other.memory_;
    }
    return *this;
}

void GpuBuffer::Release() {
    if (buffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(context_->Allocator(), buffer_, allocation_);
        buffer_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
    }
}

void GpuBuffer::Write(std::span<const std::byte> bytes, VkDeviceSize offset) {
    if (offset > size_ || bytes.size() > size_ - offset) {
        throw std::out_of_range("GpuBuffer::Write past the end of the buffer");
    }
    if (bytes.empty()) {
        return;
    }
    if (mapped_ != nullptr) {
        std::memcpy(static_cast<std::byte*>(mapped_) + offset, bytes.data(), bytes.size());
        CheckVk(vmaFlushAllocation(context_->Allocator(), allocation_, offset, bytes.size()),
                "vmaFlushAllocation");
        return;
    }
    GpuBuffer staging(*context_, bytes.size(), 0, BufferMemory::Upload);
    staging.Write(bytes);
    context_->Submit([&](VkCommandBuffer commands) {
        const VkBufferCopy region{0, offset, bytes.size()};
        vkCmdCopyBuffer(commands, staging.Handle(), buffer_, 1, &region);
    });
}

void GpuBuffer::Read(std::span<std::byte> bytes, VkDeviceSize offset) const {
    if (offset > size_ || bytes.size() > size_ - offset) {
        throw std::out_of_range("GpuBuffer::Read past the end of the buffer");
    }
    if (bytes.empty()) {
        return;
    }
    if (mapped_ != nullptr) {
        CheckVk(vmaInvalidateAllocation(context_->Allocator(), allocation_, offset, bytes.size()),
                "vmaInvalidateAllocation");
        std::memcpy(bytes.data(), static_cast<const std::byte*>(mapped_) + offset, bytes.size());
        return;
    }
    GpuBuffer staging(*context_, bytes.size(), 0, BufferMemory::Readback);
    context_->Submit([&](VkCommandBuffer commands) {
        const VkBufferCopy region{offset, 0, bytes.size()};
        vkCmdCopyBuffer(commands, buffer_, staging.Handle(), 1, &region);
    });
    staging.Read(bytes);
}

}  // namespace ernie
