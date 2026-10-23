/// @file
/// @brief The shared CUDA device, opened through the NVIDIA driver at run time, and PTX dispatch.
#pragma once

#include "terraingen/gpu/device.h"

#include <cstddef>
#include <memory>
#include <string>

namespace terraingen {

/// The first CUDA device, through its primary context and the driver API of the NVIDIA driver
/// opened at run time. Kernels run their PTX (entry point "main_0") with `SLANG_globalParams`
/// holding each buffer's device address and element count, then the push constants' address.
class CudaCompute final : public SharedComputeDevice<CudaCompute> {
  public:
    /// The API name log messages show.
    static constexpr const char* kApi = "CUDA";

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
