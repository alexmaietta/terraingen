#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace terraingen {

/// A compute kernel compiled to SPIR-V at build time, with entry point "main".
struct ComputeKernel {
    const unsigned char* spirv = nullptr;
    std::size_t size = 0;
};

/// The Vulkan compute device TerrainGen uses, opened on first use: volk opens the Vulkan loader at
/// run time, and the device is the one `TERRAINGEN_DEVICE` (an index into the physical devices)
/// names, otherwise the first discrete GPU, then integrated GPU, then any device (such as Mesa's
/// lavapipe on machines without a GPU). Safe to use from several threads; submissions are
/// serialized.
class VulkanCompute {
  public:
    /// The device, or null when Vulkan is unavailable (see `Reason`).
    static VulkanCompute* Get();

    /// Why Vulkan is unavailable, empty when it is.
    static std::string Reason();

    /// The device's name.
    const std::string& DeviceName() const;

    /// Runs `kernel` over `groups` workgroups with `buffers` bound as storage buffers 0, 1, ... of
    /// set 0 and `pushConstants` as its push constants, then copies every buffer back: each buffer
    /// is uploaded, used and read back in place.
    /// @throws std::runtime_error for a Vulkan failure.
    void Run(const ComputeKernel& kernel, const std::vector<std::span<std::byte>>& buffers,
             std::span<const std::byte> pushConstants, std::array<std::uint32_t, 3> groups);

    VulkanCompute(const VulkanCompute&) = delete;
    VulkanCompute& operator=(const VulkanCompute&) = delete;
    ~VulkanCompute();

  private:
    struct State;
    explicit VulkanCompute(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

}  // namespace terraingen
