/// @file
/// @brief `ComputeDevice`, the interface of the GPU devices, and `LazyDevice`, their shared
/// opening.
#pragma once

#include "terraingen/gpu/backend.h"
#include "terraingen/gpu/program.h"
#include "terraingen/util/log.h"
#include "terraingen/util/span.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <string>

namespace terraingen {

/// A storage buffer bound to a kernel, in binding order.
struct DeviceBuffer {
    Span<const std::byte> bytes;  ///< The host data uploaded before the run.
    std::size_t count = 0;        ///< Number of elements its structured buffer reports.
    std::byte* output = nullptr;  ///< Where its `bytes.size()` bytes are copied back after the run;
                                  ///< null for a buffer the kernel only reads.
};

/// A buffer the kernel only reads.
inline DeviceBuffer InputBuffer(Span<const std::byte> bytes, std::size_t count) {
    return {bytes, count, nullptr};
}

/// A buffer the kernel reads and writes, copied back in place after the run.
inline DeviceBuffer OutputBuffer(Span<std::byte> bytes, std::size_t count) {
    return {bytes, count, bytes.data()};
}

/// The work of one kernel run: workgroup counts and threads per workgroup, in x, y and z.
struct Dispatch {
    std::array<std::uint32_t, 3> groups{1u, 1u, 1u};     ///< Workgroup counts.
    std::array<std::uint32_t, 3> groupSize{1u, 1u, 1u};  ///< Threads per workgroup, as the kernel
                                                         ///< declares them.
};

/// A GPU that runs kernels compiled by `terraingen_add_kernel`. Thread-safe; runs are serialized.
class ComputeDevice {
  public:
    virtual ~ComputeDevice() = default;

    /// The device name the driver or system reports.
    virtual const std::string& DeviceName() const = 0;

    /// Whether `program` has a form this device runs.
    virtual bool CanRun(const KernelProgram& program) const = 0;

    /// Runs `program`, binding `buffers` in order and the push constants after them, then copies
    /// the output buffers back.
    /// @throws std::runtime_error for a device failure or a program without a form for it.
    virtual void Run(const KernelProgram& program, Span<const DeviceBuffer> buffers,
                     Span<const std::byte> pushConstants, const Dispatch& dispatch) = 0;
};

/// The device of a GPU backend (`Vulkan`, `Cuda` or `Metal`), opened on first use; null when it is
/// unavailable or `backend` is not a GPU backend.
ComputeDevice* GpuDevice(Backend backend);

/// The process-wide instance of a device type, opened by `Device::Open` on the first `Get`.
///
/// The device is never destroyed, so the driver is not torn down during exit.
template <typename Device> class LazyDevice {
  public:
    /// @param api The API name log messages show, such as "Vulkan".
    explicit LazyDevice(const char* api) : api_(api) {}

    /// The device, opened on the first call; null when it cannot open (see `Reason`).
    Device* Get() {
        std::call_once(once_, [this] {
            try {
                device_ = Device::Open().release();
                LogInfo("gpu", api_, " compute on ", device_->DeviceName());
            } catch (const std::exception& error) {
                reason_ = error.what();
                LogInfo("gpu", api_, " unavailable: ", reason_);
            }
        });
        return device_;
    }

    /// Why the device cannot open; empty when it is open.
    const std::string& Reason() {
        Get();
        return reason_;
    }

  private:
    const char* api_;
    std::once_flag once_;
    Device* device_ = nullptr;
    std::string reason_;
};

}  // namespace terraingen
