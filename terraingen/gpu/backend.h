#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace terraingen {

/// Where evaluation runs.
enum class Backend {
    Auto,    ///< A GPU when one is available, otherwise the CPU.
    Cpu,     ///< The reference C++ implementation.
    Vulkan,  ///< Vulkan compute on the device `TERRAINGEN_DEVICE` selects.
};

/// The backend named `name` ("auto", "cpu" or "vulkan"), if any.
std::optional<Backend> BackendNamed(std::string_view name);

/// The name of `backend`, as `BackendNamed` reads it.
std::string_view BackendName(Backend backend);

/// Whether a backend can run here, on which device, or why not.
struct BackendStatus {
    Backend backend = Backend::Cpu;
    bool available = false;
    std::string device;  ///< The device used, when available.
    std::string reason;  ///< Why it is not available, otherwise.
};

/// The status of the CPU and Vulkan backends. The Vulkan loader is opened (and a device chosen) the
/// first time this or a Vulkan evaluation is asked for, never at load time, so a build with Vulkan
/// support runs anywhere.
std::vector<BackendStatus> Backends();

/// The process-wide default backend: what `SetDefaultBackend` last set, otherwise the environment
/// variable `TERRAINGEN_BACKEND` ("auto", "cpu" or "vulkan"), otherwise `Backend::Auto`.
/// @throws std::invalid_argument for an unknown name in `TERRAINGEN_BACKEND`.
Backend DefaultBackend();

/// Sets the process-wide default backend, overriding `TERRAINGEN_BACKEND`.
void SetDefaultBackend(Backend backend);

/// The backend `requested` resolves to: `Auto` becomes `Vulkan` when it is available and `Cpu`
/// otherwise.
/// @throws std::runtime_error when `Vulkan` is requested but unavailable, with the reason.
Backend ResolveBackend(Backend requested);

}  // namespace terraingen
