/// @file
/// @brief Evaluation backends (CPU or Vulkan) and the process-wide default.
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

/// Looks up a backend by name ("auto", "cpu" or "vulkan"), ignoring case; `std::nullopt` for an
/// unknown name.
std::optional<Backend> BackendNamed(std::string_view name);

/// The name of a backend, as `BackendNamed` reads it; "unknown" for an out-of-range value.
std::string_view BackendName(Backend backend);

/// Whether a backend can run here, on which device, or why not.
struct BackendStatus {
    Backend backend = Backend::Cpu;  ///< The backend described.
    bool available = false;          ///< Whether it can run here.
    std::string device;              ///< The device used, when available.
    std::string reason;              ///< Why it is not available, otherwise.
};

/// The status of the CPU and Vulkan backends: one for `Cpu`, then one for `Vulkan`.
///
/// The Vulkan loader is opened (and a device chosen) the first time this or a Vulkan evaluation is
/// asked for, never at load time, so a build with Vulkan support runs anywhere.
std::vector<BackendStatus> Backends();

/// The process-wide default backend: what `SetDefaultBackend` last set, otherwise the
/// environment variable `TERRAINGEN_BACKEND` ("auto", "cpu" or "vulkan"), otherwise `Auto`.
/// @throws std::invalid_argument for an unknown name in `TERRAINGEN_BACKEND`.
Backend DefaultBackend();

/// Sets the process-wide default backend, overriding `TERRAINGEN_BACKEND`.
void SetDefaultBackend(Backend backend);

/// Resolves a requested backend to the one that runs: `requested`, except that `Auto` becomes
/// `Vulkan` when it is available and `Cpu` otherwise.
/// @throws std::runtime_error when `Vulkan` is requested but unavailable, with the reason.
Backend ResolveBackend(Backend requested);

}  // namespace terraingen
