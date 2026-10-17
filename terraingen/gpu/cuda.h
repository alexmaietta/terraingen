/// @file
/// @brief The shared CUDA device, opened through the NVIDIA driver at run time, and PTX dispatch.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace terraingen {

/// A storage buffer of a CUDA kernel: host bytes copied in and back, and the element count the
/// kernel's structured buffer reports.
struct CudaBuffer {
    std::span<std::byte> bytes;  ///< The host data.
    std::size_t count = 0;       ///< Number of elements.
};

/// The CUDA device TerrainGen uses, opened on first use.
///
/// The NVIDIA driver (libcuda.so.1, nvcuda.dll) is opened at run time and only the driver API is
/// called, so nothing of the CUDA toolkit is needed to run. The device is the first CUDA device,
/// used through its primary context. Thread-safe; launches are serialized.
class CudaCompute {
  public:
    /// The shared device, opened on the first call; null when CUDA is unavailable (see `Reason`).
    static CudaCompute* Get();

    /// Why CUDA is unavailable; empty when it is available.
    static std::string Reason();

    /// The device name the driver reports.
    const std::string& DeviceName() const;

    /// Runs a kernel Slang compiled to PTX (entry point "main_0") and copies every buffer back.
    ///
    /// Slang passes a kernel's global parameters in the constant block `SLANG_globalParams`: each
    /// structured buffer as its device address and element count, in declaration order, then the
    /// address of the push constants.
    /// @param ptx The PTX, followed by a zero byte.
    /// @param buffers The kernel's structured buffers, in declaration order.
    /// @param pushConstants The kernel's push constants.
    /// @param groups Thread block counts in x, y and z.
    /// @param groupSize Threads per block in x, y and z, as the kernel declares them.
    /// @throws std::runtime_error for a driver failure, a driver too old for the PTX, or a kernel
    /// whose global parameters are not laid out as above.
    void Run(std::span<const char> ptx, const std::vector<CudaBuffer>& buffers,
             std::span<const std::byte> pushConstants, std::array<std::uint32_t, 3> groups,
             std::array<std::uint32_t, 3> groupSize);

    CudaCompute(const CudaCompute&) = delete;
    CudaCompute& operator=(const CudaCompute&) = delete;

    /// Releases the modules and the primary context. The instance `Get` returns lives for the
    /// whole process and is never destroyed, so the driver is not torn down during exit.
    ~CudaCompute();

  private:
    struct State;
    explicit CudaCompute(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

}  // namespace terraingen
