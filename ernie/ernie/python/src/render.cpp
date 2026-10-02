#include "bindings.h"

#include "ernie/gpu/vulkan_context.h"
#include "ernie/raster/raster_delegate.h"

#include <nanobind/stl/string.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/vector.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ernie::python {

namespace {

std::unique_ptr<VulkanContext> CreateContext(bool validation, const std::string& device) {
    VulkanContextOptions options;
    options.validation = validation;
    options.preferredDevice = device;
    return VulkanContext::Create(options);
}

/// A Vulkan context and the rasterizer delegate drawing with it, destroyed in reverse order.
struct RasterRenderer {
    std::unique_ptr<VulkanContext> context;
    std::unique_ptr<RasterDelegate> delegate;

    RasterRenderer(bool validation, const std::string& device)
        : context(CreateContext(validation, device)),
          delegate(std::make_unique<RasterDelegate>(*context)) {}
};

Device ParseDevice(const std::string& device) {
    if (device == "cpu") {
        return {};
    }
    if (device == "cuda") {
        return {DeviceKind::Cuda, 0};
    }
    if (device.rfind("cuda:", 0) == 0) {
        try {
            return {DeviceKind::Cuda, std::stoi(device.substr(5))};
        } catch (const std::exception&) {
        }
    }
    throw std::invalid_argument("device must be \"cpu\", \"cuda\" or \"cuda:<index>\", not \"" +
                                device + "\"");
}

/// Holds a Python object, releasing it with the GIL held.
struct PythonReference {
    nb::object object;

    ~PythonReference() {
        if (Py_IsInitialized() != 0) {
            nb::gil_scoped_acquire gil;
            object = nb::object();
        }
    }
};

/// `tensor`, also keeping the Python object `keep` alive, so arrays in memory of the renderer's
/// Vulkan device or CUDA context cannot outlive the renderer.
Tensor KeepingAlive(const Tensor& tensor, nb::handle keep) {
    auto reference = std::make_shared<PythonReference>();
    reference->object = nb::borrow(keep);
    auto owner = std::make_shared<std::pair<Tensor, std::shared_ptr<PythonReference>>>(
        tensor, std::move(reference));
    return Tensor(tensor.Data(), tensor.Type(), tensor.Shape(), tensor.Strides(), tensor.Location(),
                  std::move(owner));
}

/// A CUDA render result: implements the DLPack protocol so a consumer's stream waits on the GPU
/// for the render before reading it.
struct CudaArray {
    Tensor tensor;
    CudaInterop* cuda = nullptr;

    nb::object Dlpack(nb::handle stream) const {
        const std::intptr_t handle = stream.is_none() ? 1 : nb::cast<std::intptr_t>(stream);
        if (handle != -1) {
            cuda->WaitForVulkan(static_cast<std::uintptr_t>(handle));
        }
        auto* kept = new Tensor(tensor);
        nb::capsule owner(kept, [](void* p) noexcept { delete static_cast<Tensor*>(p); });
        std::vector<std::size_t> shape(tensor.Shape().begin(), tensor.Shape().end());
        nb::ndarray<nb::ro> array(tensor.Data(), shape.size(), shape.data(), owner, nullptr,
                                  nb::dtype<std::uint8_t>(), nb::device::cuda::value,
                                  tensor.Location().id);
        return nb::cast(array, nb::rv_policy::reference);
    }
};

CudaInterop& RequireCuda(RasterRenderer& r) {
    std::string why;
    CudaInterop* cuda = r.delegate->Cuda(&why);
    if (cuda == nullptr) {
        throw std::invalid_argument("CUDA interop is unavailable: " + why);
    }
    return *cuda;
}

}  // namespace

void BindRender(nb::module_& m) {
    nb::class_<CudaArray>(m, "CudaArray",
                          "A render in CUDA memory. Pass it to torch.from_dlpack, cupy.from_dlpack "
                          "or jax.dlpack.from_dlpack: its __dlpack__ makes the consumer's stream "
                          "wait for the render on the GPU, without blocking the host.")
        .def(
            "__dlpack__",
            [](const CudaArray& a, nb::handle stream, nb::handle, nb::handle, nb::handle) {
                return a.Dlpack(stream);
            },
            nb::kw_only(), nb::arg("stream").none() = nb::none(),
            nb::arg("max_version").none() = nb::none(), nb::arg("dl_device").none() = nb::none(),
            nb::arg("copy").none() = nb::none())
        .def("__dlpack_device__",
             [](const CudaArray& a) {
                 return nb::make_tuple(static_cast<int>(nb::device::cuda::value),
                                       a.tensor.Location().id);
             })
        .def_prop_ro("shape", [](const CudaArray& a) {
            nb::list shape;
            for (const std::int64_t extent : a.tensor.Shape()) {
                shape.append(extent);
            }
            return nb::tuple(shape);
        });

    nb::class_<RasterRenderer>(
        m, "RasterRenderer",
        "Ernie's rasterizer: renders a Scene's terrain, material, light, camera and render "
        "settings on a Vulkan device.")
        .def(nb::init<bool, const std::string&>(), nb::arg("validation") = false,
             nb::arg("device") = "",
             "Creates a Vulkan device, preferring one whose name contains `device`, with the "
             "validation layer when `validation` is set and it is installed. Raises RuntimeError "
             "without a Vulkan device.")
        .def_prop_ro("device_name", [](const RasterRenderer& r) { return r.context->DeviceName(); })
        .def_prop_ro("brdfs",
                     [](const RasterRenderer& r) {
                         std::vector<std::string> ids;
                         for (const BrdfModel& model : r.delegate->Renderer().Brdfs().Models()) {
                             ids.push_back(model.id);
                         }
                         return ids;
                     })
        .def(
            "sync",
            [](RasterRenderer& r, const Scene& scene) {
                nb::gil_scoped_release release;
                r.delegate->Sync(scene);
            },
            nb::arg("scene"),
            "Uploads the terrain, albedo map and detail textures that changed since the last "
            "sync.")
        .def(
            "render",
            [](nb::pointer_and_handle<RasterRenderer> self, const Scene& scene,
               const Path& settings, const std::string& device) {
                const Device target = ParseDevice(device);
                Tensor image;
                {
                    nb::gil_scoped_release release;
                    image = self.p->delegate->Render(scene, settings, target);
                }
                if (target.kind == DeviceKind::Cuda) {
                    return nb::cast(
                        CudaArray{KeepingAlive(image, self.h), self.p->delegate->Cuda()});
                }
                return ArrayFromTensor(KeepingAlive(image, self.h));
            },
            nb::arg("scene"), nb::arg("settings") = Path(), nb::arg("device") = "cpu",
            "Renders `scene` as the RenderSettings prim `settings` (or the first one) describes "
            "and returns the LdrColor render var: a read-only uint8 array (rows, cols, 4). With "
            "device=\"cuda\" the array stays in GPU memory shared with Vulkan, for "
            "torch.from_dlpack and friends; it keeps the renderer alive.")
        .def_prop_ro(
            "cuda_available", [](RasterRenderer& r) { return r.delegate->Cuda() != nullptr; },
            "Whether CUDA arrays can be written to scenes this renderer draws, and rendered to.")
        .def(
            "cuda_upload",
            [](nb::pointer_and_handle<RasterRenderer> self, const AnyArray& array) {
                return ArrayFromTensor(
                    KeepingAlive(RequireCuda(*self.p).Upload(TensorFromArray(array)), self.h));
            },
            nb::arg("array"),
            "A copy of a host array in CUDA memory, for frameworks without CUDA arrays of their "
            "own.")
        .def(
            "cuda_download",
            [](RasterRenderer& r, const AnyArray& array) {
                return ArrayFromTensor(RequireCuda(r).Download(TensorFromArray(array)));
            },
            nb::arg("array"), "A host copy of a CUDA array.")
        .def(
            "take_validation_messages",
            [](RasterRenderer& r) { return r.context->TakeValidationMessages(); },
            "Vulkan validation messages since the last call.");
}

}  // namespace ernie::python
