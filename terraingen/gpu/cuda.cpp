#include "terraingen/gpu/cuda.h"

#include "terraingen/util/log.h"

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

/// The part of the CUDA driver API TerrainGen calls, declared from cuda.h so the build needs no
/// CUDA headers; the driver ABI is stable across versions.
namespace cu {

using Result = int;
using Device = int;
using Context = struct Context_st*;
using Module = struct Module_st*;
using Function = struct Function_st*;
using Stream = struct Stream_st*;
using DevicePtr = unsigned long long;

constexpr Result kSuccess = 0;
constexpr Result kErrorUnsupportedPtxVersion = 222;

}  // namespace cu

#if defined(_WIN32)
constexpr const char* kCudaDriver = "nvcuda.dll";
void* OpenLibrary(const char* name) {
    return reinterpret_cast<void*>(LoadLibraryA(name));
}
void* Symbol(void* library, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
}
#else
constexpr const char* kCudaDriver = "libcuda.so.1";
void* OpenLibrary(const char* name) {
    return dlopen(name, RTLD_NOW | RTLD_LOCAL);
}
void* Symbol(void* library, const char* name) {
    return dlsym(library, name);
}
#endif

/// The driver entry points, loaded from the driver library.
struct Driver {
    cu::Result (*init)(unsigned int) = nullptr;
    cu::Result (*deviceGetCount)(int*) = nullptr;
    cu::Result (*deviceGet)(cu::Device*, int) = nullptr;
    cu::Result (*deviceGetName)(char*, int, cu::Device) = nullptr;
    cu::Result (*primaryCtxRetain)(cu::Context*, cu::Device) = nullptr;
    cu::Result (*primaryCtxRelease)(cu::Device) = nullptr;
    cu::Result (*ctxSetCurrent)(cu::Context) = nullptr;
    cu::Result (*ctxSynchronize)() = nullptr;
    cu::Result (*moduleLoadData)(cu::Module*, const void*) = nullptr;
    cu::Result (*moduleUnload)(cu::Module) = nullptr;
    cu::Result (*moduleGetFunction)(cu::Function*, cu::Module, const char*) = nullptr;
    cu::Result (*moduleGetGlobal)(cu::DevicePtr*, std::size_t*, cu::Module, const char*) = nullptr;
    cu::Result (*memAlloc)(cu::DevicePtr*, std::size_t) = nullptr;
    cu::Result (*memFree)(cu::DevicePtr) = nullptr;
    cu::Result (*memcpyHtoD)(cu::DevicePtr, const void*, std::size_t) = nullptr;
    cu::Result (*memcpyDtoH)(void*, cu::DevicePtr, std::size_t) = nullptr;
    cu::Result (*launchKernel)(cu::Function, unsigned int, unsigned int, unsigned int, unsigned int,
                               unsigned int, unsigned int, unsigned int, cu::Stream, void**,
                               void**) = nullptr;
    cu::Result (*getErrorString)(cu::Result, const char**) = nullptr;
};

/// Sets `function` to the driver symbol `name`; false when the driver lacks it.
template <typename Function> bool Load(void* library, const char* name, Function& function) {
    function = reinterpret_cast<Function>(Symbol(library, name));
    return function != nullptr;
}

/// Loads every entry point of `driver` from `library`; the name of the first missing one, or null.
const char* LoadDriver(void* library, Driver& d) {
    const std::pair<const char*, bool> loaded[] = {
        {"cuInit", Load(library, "cuInit", d.init)},
        {"cuDeviceGetCount", Load(library, "cuDeviceGetCount", d.deviceGetCount)},
        {"cuDeviceGet", Load(library, "cuDeviceGet", d.deviceGet)},
        {"cuDeviceGetName", Load(library, "cuDeviceGetName", d.deviceGetName)},
        {"cuDevicePrimaryCtxRetain", Load(library, "cuDevicePrimaryCtxRetain", d.primaryCtxRetain)},
        {"cuDevicePrimaryCtxRelease_v2",
         Load(library, "cuDevicePrimaryCtxRelease_v2", d.primaryCtxRelease)},
        {"cuCtxSetCurrent", Load(library, "cuCtxSetCurrent", d.ctxSetCurrent)},
        {"cuCtxSynchronize", Load(library, "cuCtxSynchronize", d.ctxSynchronize)},
        {"cuModuleLoadData", Load(library, "cuModuleLoadData", d.moduleLoadData)},
        {"cuModuleUnload", Load(library, "cuModuleUnload", d.moduleUnload)},
        {"cuModuleGetFunction", Load(library, "cuModuleGetFunction", d.moduleGetFunction)},
        {"cuModuleGetGlobal_v2", Load(library, "cuModuleGetGlobal_v2", d.moduleGetGlobal)},
        {"cuMemAlloc_v2", Load(library, "cuMemAlloc_v2", d.memAlloc)},
        {"cuMemFree_v2", Load(library, "cuMemFree_v2", d.memFree)},
        {"cuMemcpyHtoD_v2", Load(library, "cuMemcpyHtoD_v2", d.memcpyHtoD)},
        {"cuMemcpyDtoH_v2", Load(library, "cuMemcpyDtoH_v2", d.memcpyDtoH)},
        {"cuLaunchKernel", Load(library, "cuLaunchKernel", d.launchKernel)},
        {"cuGetErrorString", Load(library, "cuGetErrorString", d.getErrorString)},
    };
    for (const auto& [name, ok] : loaded) {
        if (!ok) {
            return name;
        }
    }
    return nullptr;
}

/// Device allocations of one `Run`, freed when it goes out of scope however the run ends.
struct RunAllocations {
    const Driver& driver;
    std::vector<cu::DevicePtr> pointers;

    explicit RunAllocations(const Driver& d) : driver(d) {}
    RunAllocations(const RunAllocations&) = delete;
    RunAllocations& operator=(const RunAllocations&) = delete;
    ~RunAllocations() {
        for (const cu::DevicePtr pointer : pointers) {
            driver.memFree(pointer);
        }
    }
};

/// A structured buffer as Slang passes it in `SLANG_globalParams`.
struct BufferParam {
    cu::DevicePtr data;
    std::uint64_t count;
};

/// The entry point of a loaded kernel and its global parameter block.
struct Entry {
    cu::Function function = nullptr;
    cu::DevicePtr globals = 0;
    std::size_t globalsSize = 0;
};

}  // namespace

/// The driver, device, primary context and loaded modules.
struct CudaCompute::State {
    Driver driver;
    cu::Device device = 0;
    cu::Context context = nullptr;
    std::string name;
    std::mutex mutex;
    /// Modules keyed by their PTX, which lives for the whole process.
    std::map<const char*, cu::Module> modules;

    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;

    ~State() {
        if (context != nullptr) {
            driver.ctxSetCurrent(context);
            for (const auto& [ptx, module] : modules) {
                driver.moduleUnload(module);
            }
            driver.primaryCtxRelease(device);
        }
    }

    /// Throws std::runtime_error naming `what` and the driver's message unless `result` succeeded.
    void Check(cu::Result result, const char* what) const {
        if (result == cu::kSuccess) {
            return;
        }
        const char* message = nullptr;
        driver.getErrorString(result, &message);
        throw std::runtime_error(std::string("CUDA ") + what + " failed (" +
                                 (message != nullptr ? message : std::to_string(result)) + ")");
    }

    /// Opens the driver, the first device and its primary context.
    void Open() {
        void* library = OpenLibrary(kCudaDriver);
        if (library == nullptr) {
            throw std::runtime_error(std::string("no NVIDIA driver found (") + kCudaDriver +
                                     "); install the NVIDIA driver to use CUDA");
        }
        if (const char* missing = LoadDriver(library, driver)) {
            throw std::runtime_error(std::string("the NVIDIA driver lacks ") + missing +
                                     "; update the driver");
        }
        Check(driver.init(0), "initialization");
        int count = 0;
        Check(driver.deviceGetCount(&count), "device count");
        if (count == 0) {
            throw std::runtime_error("no CUDA devices");
        }
        Check(driver.deviceGet(&device, 0), "device lookup");
        char deviceName[256] = {};
        Check(driver.deviceGetName(deviceName, static_cast<int>(sizeof(deviceName)), device),
              "device name");
        name = deviceName;
        Check(driver.primaryCtxRetain(&context, device), "context creation");
    }

    /// The module of `ptx`, loaded on first use.
    cu::Module CachedModule(std::span<const char> ptx) {
        const auto found = modules.find(ptx.data());
        if (found != modules.end()) {
            return found->second;
        }
        cu::Module module = nullptr;
        const cu::Result loaded = driver.moduleLoadData(&module, ptx.data());
        if (loaded == cu::kErrorUnsupportedPtxVersion) {
            throw std::runtime_error("the NVIDIA driver is older than this build's CUDA kernels; "
                                     "update the driver");
        }
        Check(loaded, "module loading");
        return modules.emplace(ptx.data(), module).first->second;
    }

    /// The entry point "main_0" of `ptx` and its global parameters, which must hold `buffers`
    /// structured buffers and the push constants.
    Entry FindEntry(std::span<const char> ptx, std::size_t buffers) {
        const cu::Module module = CachedModule(ptx);
        Entry entry;
        Check(driver.moduleGetFunction(&entry.function, module, "main_0"), "kernel lookup");
        Check(driver.moduleGetGlobal(&entry.globals, &entry.globalsSize, module,
                                     "SLANG_globalParams"),
              "global parameter lookup");
        if (entry.globalsSize != buffers * sizeof(BufferParam) + sizeof(cu::DevicePtr)) {
            throw std::runtime_error("The CUDA kernel's global parameters are not " +
                                     std::to_string(buffers) + " buffers and push constants");
        }
        return entry;
    }

    /// Allocates device memory for `bytes`, owned by `run`, and uploads them.
    cu::DevicePtr Upload(std::span<const std::byte> bytes, RunAllocations& run) const {
        cu::DevicePtr pointer = 0;
        Check(driver.memAlloc(&pointer, std::max<std::size_t>(bytes.size(), 4)),
              "memory allocation");
        run.pointers.push_back(pointer);
        Check(driver.memcpyHtoD(pointer, bytes.data(), bytes.size()), "upload");
        return pointer;
    }

    /// Uploads the buffers, then the push constants, and points the global parameters at them.
    void UploadParams(const Entry& entry, std::span<const DeviceBuffer> buffers,
                      std::span<const std::byte> pushConstants, RunAllocations& run) const {
        std::vector<std::byte> params(entry.globalsSize);
        for (std::size_t i = 0; i < buffers.size(); ++i) {
            const BufferParam param{Upload(buffers[i].bytes, run), buffers[i].count};
            std::memcpy(params.data() + i * sizeof(BufferParam), &param, sizeof(param));
        }
        const cu::DevicePtr push = Upload(pushConstants, run);
        std::memcpy(params.data() + buffers.size() * sizeof(BufferParam), &push, sizeof(push));
        Check(driver.memcpyHtoD(entry.globals, params.data(), params.size()), "parameter upload");
    }

    /// Copies the output buffers back from their allocations, the first ones of `run`.
    void DownloadOutputs(std::span<const DeviceBuffer> buffers, const RunAllocations& run) const {
        for (std::size_t i = 0; i < buffers.size(); ++i) {
            if (buffers[i].output != nullptr) {
                Check(
                    driver.memcpyDtoH(buffers[i].output, run.pointers[i], buffers[i].bytes.size()),
                    "download");
            }
        }
    }
};

namespace {

LazyDevice<CudaCompute>& SharedDevice() {
    static LazyDevice<CudaCompute> device("CUDA");
    return device;
}

}  // namespace

CudaCompute::CudaCompute(std::unique_ptr<State> state) : state_(std::move(state)) {}

CudaCompute::~CudaCompute() = default;

std::unique_ptr<CudaCompute> CudaCompute::Open() {
    auto state = std::make_unique<State>();
    state->Open();
    return std::unique_ptr<CudaCompute>(new CudaCompute(std::move(state)));
}

CudaCompute* CudaCompute::Get() {
    return SharedDevice().Get();
}

std::string CudaCompute::Reason() {
    return SharedDevice().Reason();
}

const std::string& CudaCompute::DeviceName() const {
    return state_->name;
}

bool CudaCompute::CanRun(const KernelProgram& program) const {
    return !program.ptx.empty();
}

void CudaCompute::Run(const KernelProgram& program, std::span<const DeviceBuffer> buffers,
                      std::span<const std::byte> pushConstants, const Dispatch& dispatch) {
    if (!CanRun(program)) {
        throw std::runtime_error("The kernel has no PTX for CUDA");
    }
    State& s = *state_;
    const std::lock_guard lock(s.mutex);
    s.Check(s.driver.ctxSetCurrent(s.context), "context binding");
    const Entry entry = s.FindEntry(program.ptx, buffers.size());
    RunAllocations run(s.driver);
    s.UploadParams(entry, buffers, pushConstants, run);
    const auto& [groups, size] = dispatch;
    s.Check(s.driver.launchKernel(entry.function, groups[0], groups[1], groups[2], size[0], size[1],
                                  size[2], 0, nullptr, nullptr, nullptr),
            "kernel launch");
    s.Check(s.driver.ctxSynchronize(), "kernel");
    s.DownloadOutputs(buffers, run);
}

}  // namespace terraingen
