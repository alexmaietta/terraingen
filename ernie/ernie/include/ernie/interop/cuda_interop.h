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
/// default stream, and each call returns once its copy is complete.
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

    /// Copies a contiguous tensor on this CUDA device into `buffer` at byte `offset`, device to
    /// device.
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

    /// A contiguous CPU copy of a tensor on this CUDA device.
    /// @throws std::invalid_argument for a tensor that is not a contiguous tensor on this device.
    Tensor Download(const Tensor& device);

    /// The loaded driver and device, opaque outside the implementation.
    struct State;

  private:
    explicit CudaInterop(std::shared_ptr<State> state);
    std::uint64_t DevicePointer(GpuBuffer& buffer);
    void CheckOnDevice(const Tensor& tensor) const;

    std::shared_ptr<State> state_;
};

}  // namespace ernie
