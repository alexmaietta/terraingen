#include "ernie/gpu/gpu_buffer.h"

#include <cstring>
#include <stdexcept>
#include <utility>

namespace ernie {

namespace {

/// The index of a device-local memory type allowed by `typeBits`.
std::uint32_t DeviceLocalMemoryType(const VulkanContext& context, std::uint32_t typeBits) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(context.PhysicalDevice(), &properties);
    for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) != 0 &&
            (properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
            return i;
        }
    }
    throw std::runtime_error("No device-local memory type for a shared buffer");
}

}  // namespace

GpuBuffer::GpuBuffer(const VulkanContext& context, VkDeviceSize size, VkBufferUsageFlags usage,
                     BufferMemory memory)
    : context_(&context), size_(size), memory_(memory) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (memory == BufferMemory::Shared) {
        if (!context.SupportsExternalMemory()) {
            throw std::runtime_error("The Vulkan device cannot export memory for sharing");
        }
        VkExternalMemoryBufferCreateInfo external{
            VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO};
        external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        info.pNext = &external;
        CheckVk(vkCreateBuffer(context.Device(), &info, nullptr, &buffer_), "vkCreateBuffer");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(context.Device(), buffer_, &requirements);
        VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
        dedicated.buffer = buffer_;
        VkExportMemoryAllocateInfo exported{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
        exported.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        exported.pNext = &dedicated;
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocate.pNext = &exported;
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = DeviceLocalMemoryType(context, requirements.memoryTypeBits);
        const VkResult result =
            vkAllocateMemory(context.Device(), &allocate, nullptr, &sharedMemory_);
        if (result != VK_SUCCESS) {
            vkDestroyBuffer(context.Device(), buffer_, nullptr);
            buffer_ = VK_NULL_HANDLE;
            CheckVk(result, "vkAllocateMemory");
        }
        allocationSize_ = requirements.size;
        CheckVk(vkBindBufferMemory(context.Device(), buffer_, sharedMemory_, 0),
                "vkBindBufferMemory");
        return;
    }
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
      sharedMemory_(std::exchange(other.sharedMemory_, VK_NULL_HANDLE)),
      allocationSize_(std::exchange(other.allocationSize_, 0)),
      attached_(std::move(other.attached_)), mapped_(std::exchange(other.mapped_, nullptr)),
      size_(std::exchange(other.size_, 0)), memory_(other.memory_) {}

GpuBuffer& GpuBuffer::operator=(GpuBuffer&& other) noexcept {
    if (this != &other) {
        Release();
        context_ = other.context_;
        buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
        allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
        sharedMemory_ = std::exchange(other.sharedMemory_, VK_NULL_HANDLE);
        allocationSize_ = std::exchange(other.allocationSize_, 0);
        attached_ = std::move(other.attached_);
        mapped_ = std::exchange(other.mapped_, nullptr);
        size_ = std::exchange(other.size_, 0);
        memory_ = other.memory_;
    }
    return *this;
}

void GpuBuffer::Release() {
    attached_.reset();
    if (sharedMemory_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(context_->Device(), buffer_, nullptr);
        vkFreeMemory(context_->Device(), sharedMemory_, nullptr);
        buffer_ = VK_NULL_HANDLE;
        sharedMemory_ = VK_NULL_HANDLE;
        return;
    }
    if (buffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(context_->Allocator(), buffer_, allocation_);
        buffer_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
    }
}

int GpuBuffer::ExportFd() const {
    if (sharedMemory_ == VK_NULL_HANDLE) {
        throw std::logic_error("Only shared buffers export their memory");
    }
    VkMemoryGetFdInfoKHR info{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
    info.memory = sharedMemory_;
    info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    int fd = -1;
    CheckVk(context_->GetMemoryFd()(context_->Device(), &info, &fd), "vkGetMemoryFdKHR");
    return fd;
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
