#include "bindings.h"
#include "common.h"

#include "terraingen/gpu/backend.h"

#include <string>

namespace terraingen::python {

void BindGpu(nb::module_& m) {
    nb::enum_<Backend>(m, "Backend", "Where evaluation runs.")
        .value("AUTO", Backend::Auto, "A GPU when one is available, otherwise the CPU.")
        .value("CPU", Backend::Cpu, "The reference C++ implementation.")
        .value("VULKAN", Backend::Vulkan,
               "Vulkan compute on the device TERRAINGEN_DEVICE selects.");
    nb::class_<BackendStatus>(m, "BackendStatus",
                              "Whether a backend can run here, on which device, or why not.")
        .def_ro("backend", &BackendStatus::backend)
        .def_ro("available", &BackendStatus::available)
        .def_ro("device", &BackendStatus::device)
        .def_ro("reason", &BackendStatus::reason)
        .def("__repr__", [](const BackendStatus& s) {
            return "BackendStatus(" + std::string(BackendName(s.backend)) + ", " +
                   (s.available ? "available on " + s.device : "unavailable: " + s.reason) + ")";
        });
    m.def("backends", &Backends,
          "The status of the CPU and Vulkan backends; the Vulkan loader is opened on first use, "
          "never at import.");
    m.def("backend_named", &BackendNamed, "name"_a,
          "The backend named 'auto', 'cpu' or 'vulkan', or None.");
    m.def("default_backend", &DefaultBackend,
          "The default backend: what set_default_backend set, otherwise TERRAINGEN_BACKEND, "
          "otherwise AUTO.");
    m.def("set_default_backend", &SetDefaultBackend, "backend"_a,
          "Sets the default backend, overriding TERRAINGEN_BACKEND.");
    m.def("resolve_backend", &ResolveBackend, "backend"_a,
          "The backend `backend` resolves to: AUTO becomes VULKAN when it is available, CPU "
          "otherwise. Raises RuntimeError for VULKAN when it is unavailable.");
}

}  // namespace terraingen::python
