/// @file
/// @brief The shared Vulkan compute device and SPIR-V kernel dispatch.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace terraingen {

/// A compute kernel compiled to SPIR-V at build time, with entry point "main".
struct ComputeKernel {
    const unsigned char* spirv = nullptr;  ///< The SPIR-V words; also the pipeline cache key.
    std::size_t size = 0;                  ///< Size of `spirv` in bytes.
};

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
std::optional<std::size_t> PickDevice(std::span<const DeviceKind> kinds);

/// What to install so Vulkan runs on this platform, completing a message that says why it does
/// not: the loader and a driver on Linux (and the NVIDIA capability in Docker), MoltenVK on macOS,
/// the GPU driver on Windows.
std::string VulkanInstallHint();

/// The Vulkan compute device TerrainGen uses, opened on first use.
///
/// volk opens the Vulkan loader at run time. The device is the one `TERRAINGEN_DEVICE` (an index
/// into the physical devices) names, otherwise the first discrete GPU, then integrated GPU, then
/// any device (such as Mesa's lavapipe on machines without a GPU). Thread-safe; submissions are
/// serialized.
class VulkanCompute {
  public:
    /// The shared device, opened on the first call; null when Vulkan is unavailable (see `Reason`).
    static VulkanCompute* Get();

    /// Why Vulkan is unavailable; empty when it is available.
    static std::string Reason();

    /// The device name the driver reports.
    const std::string& DeviceName() const;

    /// Runs a kernel and copies every buffer back in place.
    /// @param buffers Host buffers bound as storage buffers 0, 1, ... of set 0.
    /// @param pushConstants The kernel's push constants, at most the device's limit in bytes.
    /// @param groups Workgroup counts in x, y and z.
    /// @throws std::runtime_error for a Vulkan failure or push constants past the device's limit.
    void Run(const ComputeKernel& kernel, const std::vector<std::span<std::byte>>& buffers,
             std::span<const std::byte> pushConstants, std::array<std::uint32_t, 3> groups);

    VulkanCompute(const VulkanCompute&) = delete;
    VulkanCompute& operator=(const VulkanCompute&) = delete;

    /// Releases the device and its cached pipelines. The instance `Get` returns lives for the
    /// whole process and is never destroyed, so the driver is not torn down during exit.
    ~VulkanCompute();

  private:
    struct State;
    explicit VulkanCompute(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

}  // namespace terraingen
