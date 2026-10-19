/// @file
/// @brief The shared Metal device on Apple platforms, and dispatch of Metal source kernels.
#pragma once

#include "terraingen/gpu/device.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string>

namespace terraingen {

/// The Metal device TerrainGen uses, opened on first use: the system's default device, which every
/// Mac has. Elsewhere it is always unavailable. Kernels run their Metal source (entry point
/// "main_0"), compiled once per process, with buffers at indices 0, 1, ... and the push constants
/// at the next index, as Slang assigns them.
class MetalCompute final : public ComputeDevice {
  public:
    /// The shared device, opened on the first call; null when Metal is unavailable (see `Reason`).
    static MetalCompute* Get();

    /// Why Metal is unavailable; empty when it is available.
    static std::string Reason();

    const std::string& DeviceName() const override;
    bool CanRun(const KernelProgram& program) const override;

    /// @throws std::runtime_error when the source does not compile or the dispatch fails.
    void Run(const KernelProgram& program, std::span<const DeviceBuffer> buffers,
             std::span<const std::byte> pushConstants, const Dispatch& dispatch) override;

    MetalCompute(const MetalCompute&) = delete;
    MetalCompute& operator=(const MetalCompute&) = delete;

    /// Releases the device and its pipelines.
    ~MetalCompute() override;

  private:
    friend class LazyDevice<MetalCompute>;
    struct State;
    explicit MetalCompute(std::unique_ptr<State> state);
    /// Opens a new device.
    /// @throws std::runtime_error saying why Metal is unavailable.
    static std::unique_ptr<MetalCompute> Open();
    std::unique_ptr<State> state_;
};

}  // namespace terraingen
