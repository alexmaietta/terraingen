// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Evaluation backends (the CPU, Vulkan, CUDA or Metal) and the process-wide default.
#pragma once

#include <array>
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
    Gpu,     ///< The best GPU backend available, whichever it is; an error when there is none.
    Cuda,    ///< CUDA on the first NVIDIA device, through the driver opened at run time.
    Metal,   ///< Metal on the system's default device; Apple platforms only.
};

/// Every backend name `BackendNamed` reads, in the order messages list them.
inline constexpr std::array<std::string_view, 6> kBackendNames{"auto",   "cpu",  "gpu",
                                                               "vulkan", "cuda", "metal"};

/// The backend names as messages list them: "auto, cpu, gpu, vulkan, cuda or metal".
std::string BackendNameList();

/// Looks up a backend by one of `kBackendNames`, ignoring case; `std::nullopt` for an unknown name.
std::optional<Backend> BackendNamed(std::string_view name);

/// The name of a backend, as `BackendNamed` reads it; "unknown" for an out-of-range value.
std::string_view BackendName(Backend backend);

/// A backend as messages show it: its name, and for an available GPU backend its device too, as in
/// "cuda (NVIDIA GeForce RTX 4090)".
std::string DescribeBackend(Backend backend);

/// Whether a backend can run here, on which device, or why not.
struct BackendStatus {
    Backend backend = Backend::Cpu;  ///< The backend described.
    bool available = false;          ///< Whether it can run here.
    std::string device;              ///< The device used, when available.
    std::string reason;              ///< Why it is not available, otherwise.
};

/// The status of every backend that runs on a device: `Cpu`, `Vulkan`, `Cuda` and `Metal`, in that
/// order.
///
/// Each GPU library (the Vulkan loader, the NVIDIA driver, Metal) is opened the first time this or
/// an evaluation on it is asked for, never at load time, so one build runs anywhere.
std::vector<BackendStatus> Backends();

/// The process-wide default backend, which evaluation uses unless told otherwise: what
/// `SetDefaultBackend` last set, otherwise the environment variable `TERRAINGEN_BACKEND` (a name
/// `BackendNamed` reads), otherwise `Cpu`, so results never change with the machine by default.
/// @throws std::invalid_argument for an unknown name in `TERRAINGEN_BACKEND`.
Backend DefaultBackend();

/// Sets the process-wide default backend, overriding `TERRAINGEN_BACKEND`.
void SetDefaultBackend(Backend backend);

/// Resolves a requested backend to the one that runs, `Cpu` or a GPU backend: `Gpu` becomes the
/// best GPU backend available (`Metal`, then `Cuda`, then `Vulkan`), `Auto` the same or `Cpu` when
/// there is none, and the others themselves.
/// @throws std::runtime_error when a GPU backend is requested but unavailable, with the reason.
Backend ResolveBackend(Backend requested);

}  // namespace terraingen
