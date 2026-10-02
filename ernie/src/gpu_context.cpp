#include "ernie/gpu_context.h"

#if defined(ERNIE_WEBGPU_WGPU_NATIVE)
#include <webgpu/wgpu.h>
#endif

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace ernie {

namespace {

void WaitForCallbacks(WGPUInstance instance, WGPUDevice device, const std::function<bool()>& done) {
    wgpuInstanceProcessEvents(instance);
    while (!done()) {
#if defined(ERNIE_WEBGPU_WGPU_NATIVE)
        if (device != nullptr) {
            wgpuDevicePoll(device, true, nullptr);
        } else {
            std::this_thread::yield();
        }
#else
        static_cast<void>(device);
        std::this_thread::yield();
#endif
        wgpuInstanceProcessEvents(instance);
    }
}

WGPUAdapter RequestAdapter(WGPUInstance instance, WGPUSurface compatibleSurface,
                           const GpuContextOptions& contextOptions) {
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.powerPreference = contextOptions.powerPreference;
    options.backendType = contextOptions.backend;
    options.compatibleSurface = compatibleSurface;

    struct Result {
        bool done = false;
        WGPUAdapter adapter = nullptr;
        std::string message;
    } result;

    WGPURequestAdapterCallbackInfo callbackInfo = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    callbackInfo.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter,
                               WGPUStringView message, void* userdata1, void*) {
        auto* out = static_cast<Result*>(userdata1);
        out->done = true;
        if (status == WGPURequestAdapterStatus_Success) {
            out->adapter = adapter;
        } else {
            out->message = ToString(message);
        }
    };
    callbackInfo.userdata1 = &result;

    wgpuInstanceRequestAdapter(instance, &options, callbackInfo);
    WaitForCallbacks(instance, nullptr, [&result] { return result.done; });
    if (result.adapter == nullptr) {
        throw std::runtime_error("No WebGPU adapter available: " + result.message);
    }
    return result.adapter;
}

WGPUDevice RequestDevice(WGPUInstance instance, WGPUAdapter adapter) {
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    descriptor.deviceLostCallbackInfo.callback = [](WGPUDevice const*, WGPUDeviceLostReason reason,
                                                    WGPUStringView message, void*, void*) {
        if (reason != WGPUDeviceLostReason_Destroyed &&
            reason != WGPUDeviceLostReason_CallbackCancelled) {
            std::fprintf(stderr, "WebGPU device lost: %s\n", ToString(message).c_str());
        }
    };
    descriptor.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType,
                                                         WGPUStringView message, void*, void*) {
        std::fprintf(stderr, "WebGPU error: %s\n", ToString(message).c_str());
    };

    struct Result {
        bool done = false;
        WGPUDevice device = nullptr;
        std::string message;
    } result;

    WGPURequestDeviceCallbackInfo callbackInfo = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    callbackInfo.callback = [](WGPURequestDeviceStatus status, WGPUDevice device,
                               WGPUStringView message, void* userdata1, void*) {
        auto* out = static_cast<Result*>(userdata1);
        out->done = true;
        if (status == WGPURequestDeviceStatus_Success) {
            out->device = device;
        } else {
            out->message = ToString(message);
        }
    };
    callbackInfo.userdata1 = &result;

    wgpuAdapterRequestDevice(adapter, &descriptor, callbackInfo);
    WaitForCallbacks(instance, nullptr, [&result] { return result.done; });
    if (result.device == nullptr) {
        throw std::runtime_error("Could not create a WebGPU device: " + result.message);
    }
    return result.device;
}

std::string DescribeAdapter(WGPUAdapter adapter) {
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(adapter, &info) != WGPUStatus_Success) {
        return "unknown adapter";
    }
    std::string name = ToString(info.device);
    if (name.empty()) {
        name = ToString(info.description);
    }
    std::string description = name + " (" + BackendName(info.backendType) + ", " +
                              AdapterTypeName(info.adapterType) + ")";
    wgpuAdapterInfoFreeMembers(info);
    return description;
}

}  // namespace

std::string ToString(WGPUStringView view) {
    if (view.data == nullptr) {
        return {};
    }
    const std::size_t length = view.length == WGPU_STRLEN ? std::strlen(view.data) : view.length;
    return std::string(view.data, length);
}

const char* AdapterTypeName(WGPUAdapterType type) {
    switch (type) {
    case WGPUAdapterType_DiscreteGPU:
        return "discrete GPU";
    case WGPUAdapterType_IntegratedGPU:
        return "integrated GPU";
    case WGPUAdapterType_CPU:
        return "CPU";
    default:
        return "unknown adapter";
    }
}

const char* BackendName(WGPUBackendType backend) {
    switch (backend) {
    case WGPUBackendType_Null:
        return "Null";
    case WGPUBackendType_WebGPU:
        return "WebGPU";
    case WGPUBackendType_D3D11:
        return "D3D11";
    case WGPUBackendType_D3D12:
        return "D3D12";
    case WGPUBackendType_Metal:
        return "Metal";
    case WGPUBackendType_Vulkan:
        return "Vulkan";
    case WGPUBackendType_OpenGL:
        return "OpenGL";
    case WGPUBackendType_OpenGLES:
        return "OpenGL ES";
    default:
        return "Unknown";
    }
}

std::unique_ptr<GpuContext> GpuContext::Create(const SurfaceFactory& createSurface,
                                               const GpuContextOptions& options) {
    std::unique_ptr<GpuContext> context(new GpuContext());

    WGPUInstanceDescriptor instanceDescriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
    context->instance_ = wgpuCreateInstance(&instanceDescriptor);
    if (context->instance_ == nullptr) {
        throw std::runtime_error("Could not create a WebGPU instance");
    }

    if (createSurface) {
        context->surface_ = createSurface(context->instance_);
        if (context->surface_ == nullptr) {
            throw std::runtime_error("Could not create a WebGPU surface");
        }
    }

    context->adapter_ = RequestAdapter(context->instance_, context->surface_, options);
    context->adapterDescription_ = DescribeAdapter(context->adapter_);
    context->device_ = RequestDevice(context->instance_, context->adapter_);
    context->queue_ = wgpuDeviceGetQueue(context->device_);

    if (context->surface_ != nullptr) {
        WGPUSurfaceCapabilities capabilities = WGPU_SURFACE_CAPABILITIES_INIT;
        if (wgpuSurfaceGetCapabilities(context->surface_, context->adapter_, &capabilities) ==
                WGPUStatus_Success &&
            capabilities.formatCount > 0) {
            context->surfaceFormat_ = capabilities.formats[0];
        }
        wgpuSurfaceCapabilitiesFreeMembers(capabilities);
        if (context->surfaceFormat_ == WGPUTextureFormat_Undefined) {
            throw std::runtime_error("The WebGPU surface reports no supported formats");
        }
    }
    return context;
}

GpuContext::~GpuContext() {
    if (surfaceConfigured_) {
        wgpuSurfaceUnconfigure(surface_);
    }
    if (surface_ != nullptr) {
        wgpuSurfaceRelease(surface_);
    }
    if (queue_ != nullptr) {
        wgpuQueueRelease(queue_);
    }
    if (device_ != nullptr) {
        wgpuDeviceRelease(device_);
    }
    if (adapter_ != nullptr) {
        wgpuAdapterRelease(adapter_);
    }
    if (instance_ != nullptr) {
        wgpuInstanceRelease(instance_);
    }
}

void GpuContext::WaitUntil(const std::function<bool()>& done) const {
    WaitForCallbacks(instance_, device_, done);
}

void GpuContext::ConfigureSurface(std::uint32_t width, std::uint32_t height) {
    if (surface_ == nullptr) {
        throw std::logic_error("ConfigureSurface called on a headless GpuContext");
    }
    WGPUSurfaceConfiguration configuration = WGPU_SURFACE_CONFIGURATION_INIT;
    configuration.device = device_;
    configuration.format = surfaceFormat_;
    configuration.usage = WGPUTextureUsage_RenderAttachment;
    configuration.width = width;
    configuration.height = height;
    configuration.alphaMode = WGPUCompositeAlphaMode_Auto;
    configuration.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(surface_, &configuration);
    surfaceConfigured_ = true;
}

}  // namespace ernie
