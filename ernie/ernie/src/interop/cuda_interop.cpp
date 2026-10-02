#include "ernie/interop/cuda_interop.h"

#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace ernie {

namespace {

/// The subset of the CUDA driver API Ernie calls, declared from cuda.h (CUDA 12) so Ernie needs
/// no CUDA headers; the driver ABI is stable across versions.
namespace cu {

using Result = int;
using Device = int;
using Context = struct Context_st*;
using Stream = struct Stream_st*;
using ExternalMemory = struct ExternalMemory_st*;
using DevicePtr = unsigned long long;

struct Uuid {
    char bytes[16];
};

/// CUDA_EXTERNAL_MEMORY_HANDLE_DESC.
struct ExternalMemoryHandleDesc {
    int type;
    union {
        int fd;
        struct {
            void* handle;
            const void* name;
        } win32;
        const void* nvSciBufObject;
    } handle;
    unsigned long long size;
    unsigned int flags;
    unsigned int reserved[16];
};
static_assert(sizeof(ExternalMemoryHandleDesc) == 104,
              "Must match CUDA_EXTERNAL_MEMORY_HANDLE_DESC");

/// CUDA_EXTERNAL_MEMORY_BUFFER_DESC.
struct ExternalMemoryBufferDesc {
    unsigned long long offset;
    unsigned long long size;
    unsigned int flags;
    unsigned int reserved[16];
};
static_assert(sizeof(ExternalMemoryBufferDesc) == 88,
              "Must match CUDA_EXTERNAL_MEMORY_BUFFER_DESC");

constexpr Result kSuccess = 0;
constexpr int kHandleTypeOpaqueFd = 1;
constexpr unsigned int kDedicated = 1;
const Stream kLegacyStream = reinterpret_cast<Stream>(0x1);

struct Driver {
    Result (*init)(unsigned int);
    Result (*deviceGetCount)(int*);
    Result (*deviceGet)(Device*, int);
    Result (*deviceGetUuid)(Uuid*, Device);
    Result (*primaryCtxRetain)(Context*, Device);
    Result (*primaryCtxRelease)(Device);
    Result (*ctxPushCurrent)(Context);
    Result (*ctxPopCurrent)(Context*);
    Result (*importExternalMemory)(ExternalMemory*, const ExternalMemoryHandleDesc*);
    Result (*externalMemoryGetMappedBuffer)(DevicePtr*, ExternalMemory,
                                            const ExternalMemoryBufferDesc*);
    Result (*destroyExternalMemory)(ExternalMemory);
    Result (*memAlloc)(DevicePtr*, std::size_t);
    Result (*memFree)(DevicePtr);
    Result (*memcpyDtoDAsync)(DevicePtr, DevicePtr, std::size_t, Stream);
    Result (*memcpyHtoDAsync)(DevicePtr, const void*, std::size_t, Stream);
    Result (*memcpyDtoHAsync)(void*, DevicePtr, std::size_t, Stream);
    Result (*streamSynchronize)(Stream);
    Result (*getErrorString)(Result, const char**);
};

}  // namespace cu

template <typename Function> bool LoadSymbol(void* library, const char* name, Function& function) {
    function = reinterpret_cast<Function>(dlsym(library, name));
    return function != nullptr;
}

}  // namespace

/// The loaded driver, the device and its retained primary context; shared by everything that
/// holds CUDA resources, so it outlives them.
struct CudaInterop::State {
    void* library = nullptr;
    cu::Driver driver{};
    cu::Device device = 0;
    int ordinal = 0;
    cu::Context context = nullptr;

    ~State() {
        if (context != nullptr) {
            driver.primaryCtxRelease(device);
        }
        if (library != nullptr) {
            dlclose(library);
        }
    }

    void Check(cu::Result result, const char* what) const {
        if (result == cu::kSuccess) {
            return;
        }
        const char* message = nullptr;
        if (driver.getErrorString != nullptr) {
            driver.getErrorString(result, &message);
        }
        throw std::runtime_error(std::string(what) + " failed: " +
                                 (message != nullptr ? message : std::to_string(result)));
    }

    /// Makes the primary context current for the scope.
    struct Scope {
        const State& state;

        explicit Scope(const State& s) : state(s) {
            state.Check(state.driver.ctxPushCurrent(state.context), "cuCtxPushCurrent");
        }

        ~Scope() {
            cu::Context popped = nullptr;
            state.driver.ctxPopCurrent(&popped);
        }
    };
};

namespace {

/// A CUDA import of one shared buffer's memory, attached to the buffer.
struct Import {
    std::shared_ptr<CudaInterop::State> state;
    cu::ExternalMemory memory = nullptr;
    cu::DevicePtr pointer = 0;

    ~Import() {
        if (memory != nullptr) {
            state->driver.destroyExternalMemory(memory);
        }
    }
};

}  // namespace

std::unique_ptr<CudaInterop> CudaInterop::Create(const VulkanContext& context, std::string* why) {
    const auto fail = [why](std::string reason) -> std::unique_ptr<CudaInterop> {
        if (why != nullptr) {
            *why = std::move(reason);
        }
        return nullptr;
    };
    if (!context.SupportsExternalMemory()) {
        return fail("the Vulkan device cannot export memory (VK_KHR_external_memory_fd)");
    }
    auto state = std::make_shared<State>();
    state->library = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
    if (state->library == nullptr) {
        return fail("the CUDA driver (libcuda.so.1) is not installed");
    }
    cu::Driver& d = state->driver;
    void* lib = state->library;
    const bool loaded =
        LoadSymbol(lib, "cuInit", d.init) &&
        LoadSymbol(lib, "cuDeviceGetCount", d.deviceGetCount) &&
        LoadSymbol(lib, "cuDeviceGet", d.deviceGet) &&
        LoadSymbol(lib, "cuDeviceGetUuid", d.deviceGetUuid) &&
        LoadSymbol(lib, "cuDevicePrimaryCtxRetain", d.primaryCtxRetain) &&
        LoadSymbol(lib, "cuDevicePrimaryCtxRelease_v2", d.primaryCtxRelease) &&
        LoadSymbol(lib, "cuCtxPushCurrent_v2", d.ctxPushCurrent) &&
        LoadSymbol(lib, "cuCtxPopCurrent_v2", d.ctxPopCurrent) &&
        LoadSymbol(lib, "cuImportExternalMemory", d.importExternalMemory) &&
        LoadSymbol(lib, "cuExternalMemoryGetMappedBuffer", d.externalMemoryGetMappedBuffer) &&
        LoadSymbol(lib, "cuDestroyExternalMemory", d.destroyExternalMemory) &&
        LoadSymbol(lib, "cuMemAlloc_v2", d.memAlloc) &&
        LoadSymbol(lib, "cuMemFree_v2", d.memFree) &&
        LoadSymbol(lib, "cuMemcpyDtoDAsync_v2", d.memcpyDtoDAsync) &&
        LoadSymbol(lib, "cuMemcpyHtoDAsync_v2", d.memcpyHtoDAsync) &&
        LoadSymbol(lib, "cuMemcpyDtoHAsync_v2", d.memcpyDtoHAsync) &&
        LoadSymbol(lib, "cuStreamSynchronize", d.streamSynchronize) &&
        LoadSymbol(lib, "cuGetErrorString", d.getErrorString);
    if (!loaded) {
        return fail("the CUDA driver lacks external memory interop (too old)");
    }
    if (d.init(0) != cu::kSuccess) {
        return fail("cuInit failed; no usable NVIDIA device");
    }
    int count = 0;
    d.deviceGetCount(&count);
    for (int ordinal = 0; ordinal < count; ++ordinal) {
        cu::Device device = 0;
        cu::Uuid uuid{};
        if (d.deviceGet(&device, ordinal) != cu::kSuccess ||
            d.deviceGetUuid(&uuid, device) != cu::kSuccess) {
            continue;
        }
        if (std::memcmp(uuid.bytes, context.DeviceUuid().data(), sizeof(uuid.bytes)) != 0) {
            continue;
        }
        state->device = device;
        state->ordinal = ordinal;
        if (d.primaryCtxRetain(&state->context, device) != cu::kSuccess) {
            state->context = nullptr;
            return fail("cannot retain the CUDA device's primary context");
        }
        return std::unique_ptr<CudaInterop>(new CudaInterop(std::move(state)));
    }
    return fail("no CUDA device matches the Vulkan device " + context.DeviceName());
}

CudaInterop::CudaInterop(std::shared_ptr<State> state) : state_(std::move(state)) {}

CudaInterop::~CudaInterop() = default;

int CudaInterop::Device() const {
    return state_->ordinal;
}

std::uint64_t CudaInterop::DevicePointer(GpuBuffer& buffer) {
    if (buffer.Memory() != BufferMemory::Shared) {
        throw std::invalid_argument("CUDA can only reach shared buffers");
    }
    if (auto* import = static_cast<Import*>(buffer.Attached().get())) {
        return import->pointer;
    }
    auto import = std::make_shared<Import>();
    import->state = state_;
    const State::Scope scope(*state_);
    cu::ExternalMemoryHandleDesc handle{};
    handle.type = cu::kHandleTypeOpaqueFd;
    handle.handle.fd = buffer.ExportFd();
    handle.size = buffer.AllocationSize();
    handle.flags = cu::kDedicated;
    const cu::Result result = state_->driver.importExternalMemory(&import->memory, &handle);
    if (result != cu::kSuccess) {
        close(handle.handle.fd);
        state_->Check(result, "cuImportExternalMemory");
    }
    cu::ExternalMemoryBufferDesc mapped{};
    mapped.size = buffer.Size();
    state_->Check(
        state_->driver.externalMemoryGetMappedBuffer(&import->pointer, import->memory, &mapped),
        "cuExternalMemoryGetMappedBuffer");
    const std::uint64_t pointer = import->pointer;
    buffer.Attach(std::move(import));
    return pointer;
}

void CudaInterop::CheckOnDevice(const Tensor& tensor) const {
    if (tensor.Location() != ernie::Device{DeviceKind::Cuda, state_->ordinal}) {
        throw std::invalid_argument("Expected a tensor on CUDA device " +
                                    std::to_string(state_->ordinal));
    }
    if (!tensor.IsContiguous()) {
        throw std::invalid_argument("Expected a contiguous CUDA tensor");
    }
}

void CudaInterop::CopyToBuffer(const Tensor& source, GpuBuffer& buffer, VkDeviceSize offset) {
    CheckOnDevice(source);
    const auto bytes = static_cast<VkDeviceSize>(source.Elements()) * SizeOf(source.Type());
    if (offset > buffer.Size() || bytes > buffer.Size() - offset) {
        throw std::invalid_argument("CudaInterop::CopyToBuffer past the end of the buffer");
    }
    const std::uint64_t destination = DevicePointer(buffer) + offset;
    if (bytes == 0) {
        return;
    }
    const State::Scope scope(*state_);
    state_->Check(state_->driver.memcpyDtoDAsync(destination,
                                                 reinterpret_cast<cu::DevicePtr>(source.Data()),
                                                 bytes, cu::kLegacyStream),
                  "cuMemcpyDtoDAsync");
    state_->Check(state_->driver.streamSynchronize(cu::kLegacyStream), "cuStreamSynchronize");
}

Tensor CudaInterop::View(std::shared_ptr<GpuBuffer> buffer, DType type,
                         std::vector<std::int64_t> shape) {
    const Tensor shaped(nullptr, type, shape);
    const auto bytes = static_cast<VkDeviceSize>(shaped.Elements()) * SizeOf(type);
    if (bytes > buffer->Size()) {
        throw std::invalid_argument("CudaInterop::View larger than the buffer");
    }
    const std::uint64_t pointer = DevicePointer(*buffer);
    return Tensor(reinterpret_cast<void*>(pointer), type, std::move(shape), {},
                  {DeviceKind::Cuda, state_->ordinal}, std::move(buffer));
}

Tensor CudaInterop::Upload(const Tensor& host) {
    if (host.Location().kind != DeviceKind::Cpu) {
        throw std::invalid_argument("CudaInterop::Upload needs a CPU tensor");
    }
    const Tensor packed = host.Contiguous();
    const std::size_t bytes = static_cast<std::size_t>(packed.Elements()) * SizeOf(packed.Type());
    const State::Scope scope(*state_);
    cu::DevicePtr pointer = 0;
    state_->Check(state_->driver.memAlloc(&pointer, std::max<std::size_t>(bytes, 1)), "cuMemAlloc");
    std::shared_ptr<void> owner(reinterpret_cast<void*>(pointer), [state = state_](void* p) {
        const State::Scope freeing(*state);
        state->driver.memFree(reinterpret_cast<cu::DevicePtr>(p));
    });
    if (bytes > 0) {
        state_->Check(
            state_->driver.memcpyHtoDAsync(pointer, packed.Data(), bytes, cu::kLegacyStream),
            "cuMemcpyHtoDAsync");
        state_->Check(state_->driver.streamSynchronize(cu::kLegacyStream), "cuStreamSynchronize");
    }
    return Tensor(reinterpret_cast<void*>(pointer), packed.Type(), packed.Shape(), {},
                  {DeviceKind::Cuda, state_->ordinal}, std::move(owner));
}

Tensor CudaInterop::Download(const Tensor& device) {
    CheckOnDevice(device);
    Tensor host = Tensor::Zeros(device.Type(), device.Shape());
    const std::size_t bytes = static_cast<std::size_t>(device.Elements()) * SizeOf(device.Type());
    if (bytes > 0) {
        const State::Scope scope(*state_);
        state_->Check(state_->driver.memcpyDtoHAsync(host.Data(),
                                                     reinterpret_cast<cu::DevicePtr>(device.Data()),
                                                     bytes, cu::kLegacyStream),
                      "cuMemcpyDtoHAsync");
        state_->Check(state_->driver.streamSynchronize(cu::kLegacyStream), "cuStreamSynchronize");
    }
    return host;
}

}  // namespace ernie
