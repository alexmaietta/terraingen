/// @file
/// @brief The shared Metal device on Apple platforms, and dispatch of Metal source kernels.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace terraingen {

/// The Metal device TerrainGen uses, opened on first use: the system's default device, which every
/// Mac has. Elsewhere it is always unavailable. Thread-safe; dispatches are serialized.
class MetalCompute {
  public:
    /// The shared device, opened on the first call; null when Metal is unavailable (see `Reason`).
    static MetalCompute* Get();

    /// Why Metal is unavailable; empty when it is available.
    static std::string Reason();

    /// The device name the system reports.
    const std::string& DeviceName() const;

    /// Runs a kernel Slang compiled to Metal source (entry point "main_0") and copies every buffer
    /// back. Buffers are bound at indices 0, 1, ... and the push constants at the next index, as
    /// Slang assigns them.
    /// @param source The Metal source, followed by a zero byte; compiled once per process.
    /// @param groups Threadgroup counts in x, y and z.
    /// @param groupSize Threads per threadgroup in x, y and z, as the kernel declares them.
    /// @throws std::runtime_error when the source does not compile or the dispatch fails.
    void Run(std::span<const char> source, const std::vector<std::span<std::byte>>& buffers,
             std::span<const std::byte> pushConstants, std::array<std::uint32_t, 3> groups,
             std::array<std::uint32_t, 3> groupSize);

    MetalCompute(const MetalCompute&) = delete;
    MetalCompute& operator=(const MetalCompute&) = delete;

    /// Releases the device and its pipelines. The instance `Get` returns lives for the whole
    /// process and is never destroyed.
    ~MetalCompute();

  private:
    struct State;
    explicit MetalCompute(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

}  // namespace terraingen
