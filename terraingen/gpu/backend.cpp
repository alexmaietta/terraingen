#include "terraingen/gpu/backend.h"

#include "terraingen/gpu/cuda.h"
#include "terraingen/gpu/metal.h"
#include "terraingen/gpu/vulkan.h"
#include "terraingen/util/text.h"

#include <atomic>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

/// The backend `SetDefaultBackend` set, or -1 for none.
std::atomic<int> gDefault{-1};

/// The backends named by `kBackendNames`, in the same order.
constexpr std::array<Backend, kBackendNames.size()> kNamedBackends{
    Backend::Auto, Backend::Cpu, Backend::Gpu, Backend::Vulkan, Backend::Cuda, Backend::Metal};

/// The GPU backends, best first: what `Gpu` and `Auto` choose from.
constexpr Backend kGpuBackends[] = {Backend::Metal, Backend::Cuda, Backend::Vulkan};

/// Why the device of a GPU backend is unavailable; empty when it is available.
std::string DeviceReason(Backend backend) {
    switch (backend) {
    case Backend::Vulkan:
        return VulkanCompute::Reason();
    case Backend::Cuda:
        return CudaCompute::Reason();
    case Backend::Metal:
        return MetalCompute::Reason();
    default:
        return std::string(BackendName(backend)) + " is not a device backend";
    }
}

/// Whether a backend runs here, on which device, or why not. CUDA and Metal also need their
/// kernels in the build (`TERRAINGEN_HAS_CUDA_KERNELS`, `TERRAINGEN_HAS_METAL_KERNELS`); without
/// CUDA kernels the driver is not even opened.
BackendStatus ProbeBackend(Backend backend) {
    BackendStatus status{backend, false, {}, {}};
    if (backend == Backend::Cpu) {
        status.available = true;
        status.device = "host";
        return status;
    }
    if (backend == Backend::Cuda && !TERRAINGEN_HAS_CUDA_KERNELS) {
        status.reason = "this build has no CUDA kernels (NVRTC does not run where it was built)";
        return status;
    }
    const ComputeDevice* device = GpuDevice(backend);
    if (device == nullptr) {
        status.reason = DeviceReason(backend);
    } else if (backend == Backend::Metal && !TERRAINGEN_HAS_METAL_KERNELS) {
        status.reason = "this build has no Metal kernels";
    } else {
        status.available = true;
        status.device = device->DeviceName();
    }
    return status;
}

/// The best GPU backend available; `std::nullopt` when there is none.
std::optional<Backend> BestGpu() {
    for (const Backend backend : kGpuBackends) {
        if (ProbeBackend(backend).available) {
            return backend;
        }
    }
    return std::nullopt;
}

}  // namespace

ComputeDevice* GpuDevice(Backend backend) {
    switch (backend) {
    case Backend::Vulkan:
        return VulkanCompute::Get();
    case Backend::Cuda:
        return CudaCompute::Get();
    case Backend::Metal:
        return MetalCompute::Get();
    default:
        return nullptr;
    }
}

std::string BackendNameList() {
    std::string list;
    for (std::size_t i = 0; i < kBackendNames.size(); ++i) {
        list += (i == 0 ? "" : i + 1 == kBackendNames.size() ? " or " : ", ");
        list += kBackendNames[i];
    }
    return list;
}

std::optional<Backend> BackendNamed(std::string_view name) {
    const std::string lower = Lower(name);
    for (std::size_t i = 0; i < kBackendNames.size(); ++i) {
        if (lower == kBackendNames[i]) {
            return kNamedBackends[i];
        }
    }
    return std::nullopt;
}

std::string_view BackendName(Backend backend) {
    for (std::size_t i = 0; i < kNamedBackends.size(); ++i) {
        if (kNamedBackends[i] == backend) {
            return kBackendNames[i];
        }
    }
    return "unknown";
}

std::string DescribeBackend(Backend backend) {
    if (backend == Backend::Vulkan || backend == Backend::Cuda || backend == Backend::Metal) {
        if (const BackendStatus status = ProbeBackend(backend); status.available) {
            return std::string(BackendName(backend)) + " (" + status.device + ")";
        }
    }
    return std::string(BackendName(backend));
}

std::vector<BackendStatus> Backends() {
    return {ProbeBackend(Backend::Cpu), ProbeBackend(Backend::Vulkan), ProbeBackend(Backend::Cuda),
            ProbeBackend(Backend::Metal)};
}

Backend DefaultBackend() {
    if (const int set = gDefault.load(); set >= 0) {
        return static_cast<Backend>(set);
    }
    const char* name = std::getenv("TERRAINGEN_BACKEND");
    if (name == nullptr || *name == '\0') {
        return Backend::Cpu;
    }
    if (const auto backend = BackendNamed(name)) {
        return *backend;
    }
    throw std::invalid_argument("TERRAINGEN_BACKEND is \"" + std::string(name) + "\"; expected " +
                                BackendNameList());
}

void SetDefaultBackend(Backend backend) {
    gDefault.store(static_cast<int>(backend));
}

Backend ResolveBackend(Backend requested) {
    switch (requested) {
    case Backend::Cpu:
        return Backend::Cpu;
    case Backend::Vulkan:
    case Backend::Cuda:
    case Backend::Metal:
        if (const BackendStatus status = ProbeBackend(requested); !status.available) {
            throw std::runtime_error("The " + std::string(BackendName(requested)) +
                                     " backend is unavailable: " + status.reason);
        }
        return requested;
    case Backend::Gpu:
        if (const std::optional<Backend> gpu = BestGpu()) {
            return *gpu;
        }
        {
            std::string reasons;
            for (const Backend backend : kGpuBackends) {
                reasons +=
                    "; " + std::string(BackendName(backend)) + ": " + ProbeBackend(backend).reason;
            }
            throw std::runtime_error("No GPU backend is available" + reasons);
        }
    case Backend::Auto:
        break;
    }
    return BestGpu().value_or(Backend::Cpu);
}

}  // namespace terraingen
