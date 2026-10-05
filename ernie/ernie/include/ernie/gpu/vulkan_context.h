#pragma once

#include <vk_mem_alloc.h>
#include <volk.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace ernie {

/// Options for `VulkanContext::Create`.
struct VulkanContextOptions {
    /// Enable the Khronos validation layer when it is installed; its messages are collected (see
    /// `VulkanContext::TakeValidationMessages`) and errors are also printed to stderr.
    bool validation = false;
    /// Prefer the first device whose name contains this text; empty prefers a discrete GPU, then an
    /// integrated one, then anything else.
    std::string preferredDevice;
    /// Extra instance extensions, e.g. the ones a window system needs for presentation.
    std::vector<std::string> instanceExtensions;
    /// Extra device extensions, e.g. `VK_KHR_swapchain`.
    std::vector<std::string> deviceExtensions;
};

/// A wait for or a signal of a timeline semaphore value in a submission.
struct TimelinePoint {
    VkSemaphore semaphore = VK_NULL_HANDLE;
    std::uint64_t value = 0;
    /// Stages that wait for the value, or that must finish before it is signalled.
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
};

/// Owns a Vulkan instance, device, graphics and compute queue, and memory allocator.
///
/// Requires Vulkan 1.3 with dynamic rendering, synchronization2, demote to helper invocation,
/// timeline semaphores and shader draw parameters, plus push descriptors. Ray queries are enabled
/// when the device supports them. Vulkan is loaded at run time through volk: the system loader on
/// Linux and Windows, MoltenVK on macOS (with the portability extensions it needs). Several
/// contexts may exist at once, on the same or different devices: core functions go through the
/// loader's dispatch, and extension functions come from the context that enabled them (e.g.
/// `PushDescriptorSet`), never from volk's globals.
class VulkanContext {
  public:
    /// @throws std::runtime_error if Vulkan cannot be loaded or no device meets the requirements.
    static std::unique_ptr<VulkanContext> Create(const VulkanContextOptions& options = {});

    ~VulkanContext();
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VkInstance Instance() const {
        return instance_;
    }

    VkPhysicalDevice PhysicalDevice() const {
        return physicalDevice_;
    }

    VkDevice Device() const {
        return device_;
    }

    VkQueue Queue() const {
        return queue_;
    }

    std::uint32_t QueueFamily() const {
        return queueFamily_;
    }

    VmaAllocator Allocator() const {
        return allocator_;
    }

    /// Device name, e.g. "NVIDIA GeForce RTX 5070 Laptop GPU".
    const std::string& DeviceName() const {
        return deviceName_;
    }

    /// Device UUID, which CUDA reports too, for matching devices across APIs.
    const std::array<std::uint8_t, VK_UUID_SIZE>& DeviceUuid() const {
        return deviceUuid_;
    }

    const VkPhysicalDeviceLimits& Limits() const {
        return limits_;
    }

    bool SupportsRayQuery() const {
        return rayQuery_;
    }

    bool SupportsAnisotropy() const {
        return anisotropy_;
    }

    /// Whether buffers can be allocated with `BufferMemory::Shared`: memory exported as POSIX file
    /// descriptors (VK_KHR_external_memory_fd), for CUDA interop.
    bool SupportsExternalMemory() const {
        return getMemoryFd_ != nullptr;
    }

    /// `vkGetMemoryFdKHR` for this device, or null without external memory support.
    PFN_vkGetMemoryFdKHR GetMemoryFd() const {
        return getMemoryFd_;
    }

    /// `vkGetSemaphoreFdKHR` for this device, or null without external memory support.
    PFN_vkGetSemaphoreFdKHR GetSemaphoreFd() const {
        return getSemaphoreFd_;
    }

    /// The index of a device-local memory type among `typeBits`.
    /// @throws std::runtime_error if there is none.
    std::uint32_t DeviceLocalMemoryType(std::uint32_t typeBits) const;

    /// Creates a timeline semaphore at `initial`, exportable as a POSIX file descriptor when
    /// `exportable` is set (which needs `SupportsExternalMemory`). The caller destroys it.
    /// @throws std::runtime_error if creation fails.
    VkSemaphore CreateTimelineSemaphore(std::uint64_t initial = 0, bool exportable = false) const;

    bool ValidationEnabled() const {
        return validation_;
    }

    /// `vkCmdPushDescriptorSetKHR` for this device.
    PFN_vkCmdPushDescriptorSetKHR PushDescriptorSet() const {
        return pushDescriptorSet_;
    }

    /// Validation and error messages reported since the last call, oldest first.
    std::vector<std::string> TakeValidationMessages();

    /// Records commands with `record` into a one-off command buffer, submits it and waits for the
    /// device to finish it. Its writes are then visible to the host and to later submissions. The
    /// submission waits for `waits` before starting and signals `signals` when done.
    /// @throws std::runtime_error if submission fails.
    void Submit(const std::function<void(VkCommandBuffer)>& record,
                std::span<const TimelinePoint> waits = {},
                std::span<const TimelinePoint> signals = {}) const;

    /// Like `Submit`, but returns once the work is submitted, without waiting for the device; its
    /// command buffer is recycled after the device finishes it. Use `signals` to tell other work
    /// (such as CUDA) when it is done, or `WaitForSubmission` with the returned number.
    /// @throws std::runtime_error if submission fails.
    std::uint64_t SubmitAsync(const std::function<void(VkCommandBuffer)>& record,
                              std::span<const TimelinePoint> waits = {},
                              std::span<const TimelinePoint> signals = {}) const;

    /// Waits until the device has finished every `SubmitAsync` submission.
    void WaitForSubmissions() const;

    /// Waits up to `timeoutNs` nanoseconds (0 polls) for `SubmitAsync` submission `submission`;
    /// true once the device has finished it.
    bool WaitForSubmission(std::uint64_t submission, std::uint64_t timeoutNs = UINT64_MAX) const;

    /// Whether `SubmitAsync` work is still pending on the device.
    bool HasPendingSubmissions() const;

  private:
    VulkanContext() = default;
    void Report(std::string message);

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t queueFamily_ = 0;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger_ = nullptr;
    PFN_vkCmdPushDescriptorSetKHR pushDescriptorSet_ = nullptr;
    PFN_vkGetMemoryFdKHR getMemoryFd_ = nullptr;
    PFN_vkGetSemaphoreFdKHR getSemaphoreFd_ = nullptr;
    std::string deviceName_;
    std::array<std::uint8_t, VK_UUID_SIZE> deviceUuid_{};
    VkPhysicalDeviceLimits limits_{};
    bool rayQuery_ = false;
    bool anisotropy_ = false;
    bool validation_ = false;
    mutable std::mutex submitMutex_;
    /// Timeline signalled by every `SubmitAsync`, and its command buffers awaiting recycling.
    VkSemaphore submissions_ = VK_NULL_HANDLE;
    mutable std::uint64_t submitted_ = 0;
    mutable std::vector<std::pair<VkCommandBuffer, std::uint64_t>> pending_;

    VkCommandBuffer Record(const std::function<void(VkCommandBuffer)>& record) const;
    void RecyclePending() const;
    std::unique_ptr<std::function<void(std::string)>> sink_;
    std::mutex messagesMutex_;
    std::vector<std::string> messages_;
};

/// Loads Vulkan (the system loader, or MoltenVK on macOS without one) and returns its
/// `vkGetInstanceProcAddr`, e.g. for `glfwInitVulkanLoader` so a window system uses the same
/// Vulkan as Ernie.
/// @throws std::runtime_error if Vulkan is not available.
PFN_vkGetInstanceProcAddr LoadVulkanLoader();

/// Throws `std::runtime_error` naming `what` and the result unless `result` is `VK_SUCCESS`.
void CheckVk(VkResult result, const char* what);

}  // namespace ernie
