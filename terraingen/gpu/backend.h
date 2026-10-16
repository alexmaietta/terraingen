/// @file
/// @brief Evaluation backends (CPU or Vulkan) and the process-wide default.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace terraingen {

/// @brief Where evaluation runs.
enum class Backend {
    Auto,    ///< A GPU when one is available, otherwise the CPU.
    Cpu,     ///< The reference C++ implementation.
    Vulkan,  ///< Vulkan compute on the device `TERRAINGEN_DEVICE` selects.
};

/// @brief Looks up a backend by name, ignoring case.
/// @param name "auto", "cpu" or "vulkan".
/// @return The backend, or `std::nullopt` for an unknown name.
std::optional<Backend> BackendNamed(std::string_view name);

/// @brief The name of a backend, as `BackendNamed` reads it.
/// @param backend The backend to name.
/// @return "auto", "cpu" or "vulkan"; "unknown" for an out-of-range value.
std::string_view BackendName(Backend backend);

/// @brief Whether a backend can run here, on which device, or why not.
struct BackendStatus {
    Backend backend = Backend::Cpu;  ///< The backend described.
    bool available = false;          ///< Whether it can run here.
    std::string device;              ///< The device used, when available.
    std::string reason;              ///< Why it is not available, otherwise.
};

/// @brief The status of the CPU and Vulkan backends.
///
/// The Vulkan loader is opened (and a device chosen) the first time this or a Vulkan evaluation is
/// asked for, never at load time, so a build with Vulkan support runs anywhere.
/// @return One status for `Cpu`, then one for `Vulkan`.
std::vector<BackendStatus> Backends();

/// @brief The process-wide default backend.
/// @return What `SetDefaultBackend` last set, otherwise the environment variable
///         `TERRAINGEN_BACKEND` ("auto", "cpu" or "vulkan"), otherwise `Backend::Auto`.
/// @throws std::invalid_argument for an unknown name in `TERRAINGEN_BACKEND`.
Backend DefaultBackend();

/// @brief Sets the process-wide default backend, overriding `TERRAINGEN_BACKEND`.
/// @param backend The new default.
void SetDefaultBackend(Backend backend);

/// @brief Resolves a requested backend to the one that runs.
/// @param requested The backend asked for.
/// @return `requested`, except that `Auto` becomes `Vulkan` when it is available and `Cpu`
///         otherwise.
/// @throws std::runtime_error when `Vulkan` is requested but unavailable, with the reason.
Backend ResolveBackend(Backend requested);

}  // namespace terraingen
