#pragma once

#include <webgpu/webgpu.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace terraingen::render {

/// Adapter selection preferences for `GpuContext::Create`.
struct GpuContextOptions {
    WGPUBackendType backend = WGPUBackendType_Undefined;  ///< Undefined lets WebGPU choose.
    WGPUPowerPreference powerPreference = WGPUPowerPreference_HighPerformance;
};

/// Owns a WebGPU instance, adapter, device and queue, plus an optional presentation surface.
///
/// Built on the standard `webgpu.h` C API, so the same code runs on wgpu-native, Dawn and browser
/// WebGPU. Device errors are reported on stderr.
class GpuContext {
  public:
    /// Creates a surface for `instance`, typically from a window.
    using SurfaceFactory = std::function<WGPUSurface(WGPUInstance instance)>;

    /// Creates a context, choosing an adapter that can present to the surface from `createSurface`
    /// if given, or a headless context otherwise.
    /// @throws std::runtime_error if no suitable adapter or device is available, or the surface
    /// cannot be created.
    static std::unique_ptr<GpuContext> Create(const SurfaceFactory& createSurface = {},
                                              const GpuContextOptions& options = {});

    ~GpuContext();
    GpuContext(const GpuContext&) = delete;
    GpuContext& operator=(const GpuContext&) = delete;

    WGPUInstance Instance() const {
        return instance_;
    }

    WGPUAdapter Adapter() const {
        return adapter_;
    }

    WGPUDevice Device() const {
        return device_;
    }

    WGPUQueue Queue() const {
        return queue_;
    }

    /// Presentation surface, or null for a headless context.
    WGPUSurface Surface() const {
        return surface_;
    }

    /// Preferred surface texture format, or `WGPUTextureFormat_Undefined` for a headless context.
    WGPUTextureFormat SurfaceFormat() const {
        return surfaceFormat_;
    }

    /// Human-readable adapter summary, e.g. "Intel(R) Graphics (Vulkan, integrated GPU)".
    const std::string& AdapterDescription() const {
        return adapterDescription_;
    }

    /// Pumps WebGPU callbacks until `done` returns true.
    ///
    /// Asynchronous operations must register callbacks with `WGPUCallbackMode_AllowProcessEvents`;
    /// they fire inside this call. Blocks on outstanding GPU work between checks.
    void WaitUntil(const std::function<bool()>& done) const;

    /// Configures the surface for a framebuffer of `width` x `height` pixels with vsync.
    /// @throws std::logic_error for a headless context.
    void ConfigureSurface(std::uint32_t width, std::uint32_t height);

  private:
    GpuContext() = default;

    WGPUInstance instance_ = nullptr;
    WGPUSurface surface_ = nullptr;
    WGPUAdapter adapter_ = nullptr;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUTextureFormat surfaceFormat_ = WGPUTextureFormat_Undefined;
    bool surfaceConfigured_ = false;
    std::string adapterDescription_;
};

/// Converts a `WGPUStringView`, which may be null-terminated or length-delimited, to a string.
std::string ToString(WGPUStringView view);

/// Name of a WebGPU backend, e.g. "Vulkan".
const char* BackendName(WGPUBackendType backend);

/// Name of an adapter type, e.g. "integrated GPU".
const char* AdapterTypeName(WGPUAdapterType type);

}  // namespace terraingen::render
