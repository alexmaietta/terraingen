#pragma once

#include "ernie/gpu/gpu_buffer.h"
#include "ernie/gpu/vulkan_context.h"
#include "ernie/interop/tensor.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ernie {

/// Zero-copy sharing between a Vulkan device and the CUDA device behind it, so arrays from CUDA
/// frameworks (PyTorch, JAX, CuPy, Warp) reach Ernie's buffers, and Ernie's results reach them,
/// without passing through host memory.
///
/// Vulkan memory is exported as POSIX file descriptors (`BufferMemory::Shared`) and imported into
/// CUDA as external memory. The CUDA driver (libcuda) is loaded at run time, so Ernie has no build
/// dependency on the CUDA toolkit; without an NVIDIA driver `Create` reports CUDA unavailable.
/// CUDA work runs in the device's primary context, the one CUDA frameworks use, on the legacy
/// default stream, so it is ordered after work that frameworks hand over through DLPack.
///
/// The two sides order their work on the GPU, without blocking the host, through two timeline
/// semaphores created in Vulkan and imported into CUDA, one per direction so each side signals
/// only its own:
/// - CUDA to Vulkan: `CopyToBuffer` queues its copy and signals the next value; Vulkan
///   submissions that read the buffer wait for `VulkanWait`.
/// - Vulkan to CUDA: a Vulkan submission whose writes CUDA reads signals `NextVulkanSignal`; CUDA
///   streams wait for it with `WaitForVulkan`, and `Download` waits for it itself.
///
/// References:
/// - NVIDIA, CUDA Driver API, "External Resource Interoperability",
///   https://docs.nvidia.com/cuda/cuda-driver-api/group__CUDA__EXTRES__INTEROP.html
/// - Khronos, VK_KHR_external_memory_fd,
///   https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_external_memory_fd.html
class CudaInterop {
  public:
    /// Connects `context`'s device to the CUDA device with the same UUID.
    /// @return Null if the CUDA driver cannot be loaded, no CUDA device matches, or the Vulkan
    /// device cannot export memory; `why`, if given, says which.
    static std::unique_ptr<CudaInterop> Create(const VulkanContext& context,
                                               std::string* why = nullptr);
    ~CudaInterop();
    CudaInterop(const CudaInterop&) = delete;
    CudaInterop& operator=(const CudaInterop&) = delete;

    /// The CUDA device ordinal.
    int Device() const;

    /// Queues a device-to-device copy of a contiguous tensor on this CUDA device into `buffer` at
    /// byte `offset` and returns without waiting; Vulkan work reading the buffer must wait for
    /// `VulkanWait`. The tensor must not be written again until that work is done.
    /// @throws std::invalid_argument if the tensor is not a contiguous tensor on this device, the
    /// buffer is not `BufferMemory::Shared`, or the bytes do not fit.
    void CopyToBuffer(const Tensor& source, GpuBuffer& buffer, VkDeviceSize offset = 0);

    /// A CUDA tensor of `type` and `shape` over `buffer`'s memory from its start, which keeps the
    /// buffer alive. The buffer's Vulkan context must outlive the tensor.
    /// @throws std::invalid_argument if the buffer is not `BufferMemory::Shared` or too small.
    Tensor View(std::shared_ptr<GpuBuffer> buffer, DType type, std::vector<std::int64_t> shape);

    /// A contiguous copy of a CPU tensor in CUDA memory that the tensor owns.
    /// @throws std::invalid_argument for a tensor that is not on the CPU.
    Tensor Upload(const Tensor& host);

    /// A contiguous CPU copy of a tensor on this CUDA device, after the latest Vulkan signal.
    /// @throws std::invalid_argument for a tensor that is not a contiguous tensor on this device.
    Tensor Download(const Tensor& device);

    /// The wait Vulkan submissions that read buffers written by `CopyToBuffer` need; a null
    /// semaphore before the first copy.
    TimelinePoint VulkanWait() const;

    /// A signal to add to the next Vulkan submission whose writes CUDA will read.
    TimelinePoint NextVulkanSignal();

    /// Makes the CUDA stream `stream` wait on the GPU, without blocking the host, for the latest
    /// `NextVulkanSignal`. Streams follow DLPack: 1 is the legacy default stream, 2 the
    /// per-thread default stream, anything else a CUstream handle.
    void WaitForVulkan(std::uintptr_t stream = 1);

    /// Blocks the host until every copy queued so far is done.
    void Synchronize();

    /// The loaded driver and device, opaque outside the implementation.
    struct State;

  private:
    explicit CudaInterop(std::shared_ptr<State> state);
    std::uint64_t DevicePointer(GpuBuffer& buffer);
    void CheckOnDevice(const Tensor& tensor) const;

    std::shared_ptr<State> state_;
};

}  // namespace ernie
