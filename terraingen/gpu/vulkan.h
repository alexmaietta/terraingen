/// @file
/// @brief The shared Vulkan compute device and SPIR-V kernel dispatch.
#pragma once

#include "terraingen/gpu/device.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

namespace terraingen {

/// The kind of a Vulkan device, as the driver reports it.
enum class DeviceKind {
    Discrete,    ///< A discrete GPU.
    Integrated,  ///< A GPU sharing memory with the CPU.
    Virtual,     ///< A GPU in a virtual machine.
    Cpu,         ///< Software Vulkan on the CPU, such as Mesa's lavapipe.
    Other,       ///< Anything else.
};

/// The device chosen automatically among devices of `kinds`: the first discrete GPU, then
/// integrated, virtual and other devices. Software devices are never chosen, since they are slower
/// than the CPU backend; `TERRAINGEN_DEVICE` can still name one.
/// @return Its index, or `std::nullopt` when every device is a software one or there is none.
std::optional<std::size_t> PickDevice(Span<const DeviceKind> kinds);

/// What to install so Vulkan runs on this platform, completing a message that says why it does
/// not: the loader and a driver on Linux (and the NVIDIA capability in Docker), MoltenVK on macOS,
/// the GPU driver on Windows.
std::string VulkanInstallHint();

/// The Vulkan compute device TerrainGen uses, opened on first use.
///
/// volk opens the Vulkan loader at run time. The device is the one `TERRAINGEN_DEVICE` (an index
/// into the physical devices) names, otherwise the one `PickDevice` picks; software devices such
/// as Mesa's lavapipe are used only when `TERRAINGEN_DEVICE` names them. Kernels run their SPIR-V
/// (entry point "main") with the buffers as storage buffers 0, 1, ... of set 0 and the workgroup
/// size the SPIR-V declares.
class VulkanCompute final : public ComputeDevice {
  public:
    /// The shared device, opened on the first call; null when Vulkan is unavailable (see `Reason`).
    static VulkanCompute* Get();

    /// Why Vulkan is unavailable; empty when it is available.
    static std::string Reason();

    const std::string& DeviceName() const override;
    bool CanRun(const KernelProgram& program) const override;

    /// @throws std::runtime_error for a Vulkan failure or push constants past the device's limit.
    void Run(const KernelProgram& program, Span<const DeviceBuffer> buffers,
             Span<const std::byte> pushConstants, const Dispatch& dispatch) override;

    VulkanCompute(const VulkanCompute&) = delete;
    VulkanCompute& operator=(const VulkanCompute&) = delete;

    /// Releases the device and its cached pipelines.
    ~VulkanCompute() override;

  private:
    friend class LazyDevice<VulkanCompute>;
    struct State;
    explicit VulkanCompute(std::unique_ptr<State> state);
    /// Opens a new device.
    /// @throws std::runtime_error saying why Vulkan is unavailable.
    static std::unique_ptr<VulkanCompute> Open();
    std::unique_ptr<State> state_;
};

}  // namespace terraingen
