/// @file
/// @brief The shared CUDA device, opened through the NVIDIA driver at run time, and PTX dispatch.
#pragma once

#include "terraingen/gpu/device.h"

#include <cstddef>
#include <memory>
#include <string>

namespace terraingen {

/// The CUDA device TerrainGen uses, opened on first use.
///
/// The NVIDIA driver (libcuda.so.1, nvcuda.dll) is opened at run time and only the driver API is
/// called, so nothing of the CUDA toolkit is needed to run. The device is the first CUDA device,
/// used through its primary context. Kernels run their PTX (entry point "main_0"); Slang passes a
/// kernel's global parameters in the constant block `SLANG_globalParams`: each structured buffer as
/// its device address and element count, in declaration order, then the address of the push
/// constants.
class CudaCompute final : public ComputeDevice {
  public:
    /// The shared device, opened on the first call; null when CUDA is unavailable (see `Reason`).
    static CudaCompute* Get();

    /// Why CUDA is unavailable; empty when it is available.
    static std::string Reason();

    const std::string& DeviceName() const override;
    bool CanRun(const KernelProgram& program) const override;

    /// @throws std::runtime_error for a driver failure, a driver too old for the PTX, or a kernel
    /// whose global parameters are not laid out as above.
    void Run(const KernelProgram& program, Span<const DeviceBuffer> buffers,
             Span<const std::byte> pushConstants, const Dispatch& dispatch) override;

    CudaCompute(const CudaCompute&) = delete;
    CudaCompute& operator=(const CudaCompute&) = delete;

    /// Releases the modules and the primary context.
    ~CudaCompute() override;

  private:
    friend class LazyDevice<CudaCompute>;
    struct State;
    explicit CudaCompute(std::unique_ptr<State> state);
    /// Opens a new device.
    /// @throws std::runtime_error saying why CUDA is unavailable.
    static std::unique_ptr<CudaCompute> Open();
    std::unique_ptr<State> state_;
};

}  // namespace terraingen
