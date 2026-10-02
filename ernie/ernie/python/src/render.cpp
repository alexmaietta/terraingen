#include "bindings.h"

#include "ernie/gpu/vulkan_context.h"
#include "ernie/raster/raster_delegate.h"

#include <nanobind/stl/map.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/vector.h>

#include <chrono>
#include <memory>
#include <optional>
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

/// A render var of a step's outputs, keeping its renderer alive.
struct PyRenderVar {
    RenderVar var;
    nb::object renderer;
};

/// A render var mapped as a CUDA array: the frame's own image, read in place.
struct PyCudaArrayMapping {
    CudaArrayView view;
    PyRenderVar var;
};

/// One frame of a render product, as ovrtx's: its render vars by path.
struct PyFrame {
    std::map<std::string, PyRenderVar> renderVars;
};

/// The outputs of one render product of a step.
struct PyProductOutput {
    std::string product;
    std::vector<PyFrame> frames;
};

using PyOutputs = std::map<std::string, PyProductOutput>;

PyOutputs Wrap(const RenderProductSetOutputs& outputs, nb::handle renderer) {
    PyOutputs wrapped;
    for (const auto& [path, output] : outputs) {
        PyProductOutput product;
        product.product = path.String();
        PyFrame frame;
        for (const auto& [name, var] : output.vars) {
            frame.renderVars.emplace(var.GetPath().String(),
                                     PyRenderVar{var, nb::borrow(renderer)});
        }
        product.frames.push_back(std::move(frame));
        wrapped.emplace(path.String(), std::move(product));
    }
    return wrapped;
}

/// A submitted step, as ovrtx's `Operation`.
struct PyOperation {
    std::optional<StepOperation> operation;
    nb::object renderer;
};

std::uintptr_t StreamOf(nb::handle stream) {
    return stream.is_none() ? 1 : nb::cast<std::uintptr_t>(stream);
}

std::vector<Path> PathsOf(nb::handle products) {
    std::vector<Path> paths;
    if (products.is_none()) {
        return paths;
    }
    for (nb::handle product : products) {
        paths.push_back(nb::isinstance<Path>(product) ? nb::cast<Path>(product)
                                                      : Path(nb::cast<std::string>(product)));
    }
    return paths;
}

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

    nb::enum_<MapDevice>(m, "Device", "Where RenderVar.map delivers an output, as ovrtx's Device.")
        .value("CPU", MapDevice::Cpu, "A NumPy array; the texels are copied to the host.")
        .value("CUDA", MapDevice::Cuda,
               "A linear CUDA array the GPU fills from the render, without the host.")
        .value("CUDA_ARRAY", MapDevice::CudaArray,
               "The rendered image itself as a CUDA array (CUarray): no copy at all.");

    nb::class_<PyCudaArrayMapping>(
        m, "CudaArrayMapping",
        "A render var mapped as a CUDA array: the frame's image, read in place with CUDA texture "
        "or surface APIs or wp.Texture2D(cuda_array=mapping.cuda_array). Unmap the render var "
        "when done.")
        .def_prop_ro(
            "cuda_array", [](const PyCudaArrayMapping& mapping) { return mapping.view.array; },
            "The CUarray handle.")
        .def_prop_ro("shape",
                     [](const PyCudaArrayMapping& mapping) {
                         return nb::make_tuple(mapping.view.height, mapping.view.width,
                                               mapping.view.channels);
                     })
        .def_prop_ro("device_id",
                     [](const PyCudaArrayMapping& mapping) { return mapping.view.device; });

    nb::class_<PyRenderVar>(m, "RenderVar",
                            "One output of a rendered frame, such as LdrColor, as UsdRender's "
                            "RenderVar.")
        .def_prop_ro("path", [](const PyRenderVar& v) { return v.var.GetPath(); })
        .def_prop_ro("name", [](const PyRenderVar& v) { return v.var.Name(); })
        .def_prop_ro(
            "shape",
            [](const PyRenderVar& v) { return nb::make_tuple(v.var.Height(), v.var.Width(), 4); })
        .def(
            "map",
            [](PyRenderVar& v, MapDevice device, nb::handle syncStream) -> nb::object {
                MappedRenderVar mapped;
                {
                    nb::gil_scoped_release release;
                    mapped = v.var.Map(device, StreamOf(syncStream));
                }
                if (device == MapDevice::CudaArray) {
                    return nb::cast(PyCudaArrayMapping{mapped.array, v});
                }
                return ArrayFromTensor(KeepingAlive(mapped.tensor, v.renderer));
            },
            nb::arg("device") = MapDevice::Cpu, nb::arg("sync_stream").none() = nb::none(),
            "Maps the output: Device.CPU waits for the render and returns a NumPy array; "
            "Device.CUDA and Device.CUDA_ARRAY make CUDA stream `sync_stream` (a CUstream handle "
            "or DLPack stream number, default the legacy stream) wait for the render on the GPU "
            "and return at once.")
        .def(
            "unmap", [](PyRenderVar& v, nb::handle stream) { v.var.Unmap(StreamOf(stream)); },
            nb::arg("stream").none() = nb::none(),
            "Ends a CUDA_ARRAY mapping; the renderer waits for the work queued on `stream` before "
            "rendering into the frame again.")
        .def_prop_ro("mapped", [](const PyRenderVar& v) { return v.var.IsMapped(); });

    nb::class_<PyFrame>(m, "Frame", "A rendered frame of a render product.")
        .def_ro("render_vars", &PyFrame::renderVars, "Render vars by RenderVar path.");

    nb::class_<PyProductOutput>(m, "RenderProductOutput", "The outputs of one render product.")
        .def_ro("product", &PyProductOutput::product)
        .def_ro("frames", &PyProductOutput::frames);

    nb::class_<PyOperation>(m, "Operation",
                            "A submitted step, as ovrtx's Operation: wait or poll, then fetch.")
        .def(
            "wait",
            [](nb::handle self, std::optional<std::int64_t> timeoutNs) -> nb::object {
                PyOperation& op = nb::cast<PyOperation&>(self);
                bool done = false;
                {
                    nb::gil_scoped_release release;
                    done = op.operation->Wait(timeoutNs ? std::chrono::nanoseconds(*timeoutNs)
                                                        : std::chrono::nanoseconds::max());
                }
                return done ? nb::borrow(self) : nb::none();
            },
            nb::arg("timeout_ns").none() = nb::none(),
            "Waits up to `timeout_ns` nanoseconds (None: forever, 0: poll) for the step; returns "
            "the operation once it is done, for .fetch(), or None while it is pending.")
        .def_prop_ro("done", [](PyOperation& op) { return op.operation->Done(); })
        .def(
            "fetch",
            [](PyOperation& op) {
                RenderProductSetOutputs outputs;
                {
                    nb::gil_scoped_release release;
                    outputs = op.operation->Fetch();
                }
                return Wrap(outputs, op.renderer);
            },
            "The step's outputs by render product path, waiting for the step first.")
        .def(
            "outputs", [](PyOperation& op) { return Wrap(op.operation->Outputs(), op.renderer); },
            "The step's outputs without waiting, for CUDA mappings that order on the GPU.");

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
        .def(
            "step_async",
            [](nb::pointer_and_handle<RasterRenderer> self, const Scene& scene,
               nb::handle renderProducts, const Path& settings) {
                const std::vector<Path> products = PathsOf(renderProducts);
                PyOperation op;
                op.renderer = nb::borrow(self.h);
                {
                    nb::gil_scoped_release release;
                    op.operation = self.p->delegate->StepAsync(scene, products, settings);
                }
                return op;
            },
            nb::arg("scene"), nb::arg("render_products").none() = nb::none(),
            nb::arg("settings") = Path(),
            "Renders the RenderProduct prims `render_products` (or the settings' products, or a "
            "default one) and returns an Operation at once, as ovrtx's step_async.")
        .def(
            "step",
            [](nb::pointer_and_handle<RasterRenderer> self, const Scene& scene,
               nb::handle renderProducts, const Path& settings) {
                const std::vector<Path> products = PathsOf(renderProducts);
                RenderProductSetOutputs outputs;
                {
                    nb::gil_scoped_release release;
                    outputs = self.p->delegate->Step(scene, products, settings);
                }
                return Wrap(outputs, self.h);
            },
            nb::arg("scene"), nb::arg("render_products").none() = nb::none(),
            nb::arg("settings") = Path(),
            "step_async(...).wait().fetch(): the outputs by render product path, each with "
            ".frames[0].render_vars by RenderVar path.")
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
