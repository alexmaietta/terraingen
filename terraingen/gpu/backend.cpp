#include "terraingen/gpu/backend.h"

#include "terraingen/gpu/cuda.h"
#include "terraingen/gpu/metal.h"
#include "terraingen/gpu/vulkan.h"

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

/// The backend `SetDefaultBackend` set, or -1 for none.
std::atomic<int> gDefault{-1};

/// The GPU backends, best first: what `Gpu` and `Auto` choose from.
constexpr Backend kGpuBackends[] = {Backend::Metal, Backend::Cuda, Backend::Vulkan};

/// Whether a backend runs here, on which device, or why not. CUDA and Metal also need their
/// kernels in the build (`TERRAINGEN_HAS_CUDA_KERNELS`, `TERRAINGEN_HAS_METAL_KERNELS`).
BackendStatus ProbeBackend(Backend backend) {
    BackendStatus status{backend, false, {}, {}};
    const auto from = [&](const auto* compute, const std::string& reason) {
        status.available = compute != nullptr;
        if (compute != nullptr) {
            status.device = compute->DeviceName();
        } else {
            status.reason = reason;
        }
    };
    switch (backend) {
    case Backend::Cpu:
        status.available = true;
        status.device = "host";
        break;
    case Backend::Vulkan:
        from(VulkanCompute::Get(), VulkanCompute::Reason());
        break;
    case Backend::Cuda:
        if (TERRAINGEN_HAS_CUDA_KERNELS) {
            from(CudaCompute::Get(), CudaCompute::Reason());
        } else {
            status.reason =
                "this build has no CUDA kernels (NVRTC does not run where it was built)";
        }
        break;
    case Backend::Metal:
        if (TERRAINGEN_HAS_METAL_KERNELS) {
            from(MetalCompute::Get(), MetalCompute::Reason());
        } else {
            status.reason = MetalCompute::Reason();
        }
        break;
    default:
        status.reason = std::string(BackendName(backend)) + " is not a device backend";
        break;
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

std::optional<Backend> BackendNamed(std::string_view name) {
    std::string lower(name);
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (const Backend backend : {Backend::Auto, Backend::Cpu, Backend::Gpu, Backend::Vulkan,
                                  Backend::Cuda, Backend::Metal}) {
        if (lower == BackendName(backend)) {
            return backend;
        }
    }
    return std::nullopt;
}

std::string_view BackendName(Backend backend) {
    switch (backend) {
    case Backend::Auto:
        return "auto";
    case Backend::Cpu:
        return "cpu";
    case Backend::Vulkan:
        return "vulkan";
    case Backend::Gpu:
        return "gpu";
    case Backend::Cuda:
        return "cuda";
    case Backend::Metal:
        return "metal";
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
    throw std::invalid_argument("TERRAINGEN_BACKEND is \"" + std::string(name) +
                                "\"; expected auto, cpu, gpu, vulkan, cuda or metal");
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
