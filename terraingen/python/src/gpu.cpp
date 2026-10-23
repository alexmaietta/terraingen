// SPDX-License-Identifier: BSD-3-Clause

#include "bindings.h"
#include "common.h"

#include "terraingen/gpu/backend.h"

#include <string>

namespace terraingen::python {

void BindGpu(nb::module_& m) {
    nb::enum_<Backend>(m, "Backend", "Where evaluation runs.")
        .value("AUTO", Backend::Auto, "A GPU when one is available, otherwise the CPU.")
        .value("CPU", Backend::Cpu, "The reference C++ implementation.")
        .value("VULKAN", Backend::Vulkan, "Vulkan compute on the device TERRAINGEN_DEVICE selects.")
        .value("GPU", Backend::Gpu,
               "The best GPU backend available, whichever it is; an error when there is none.")
        .value("CUDA", Backend::Cuda,
               "CUDA on the first NVIDIA device, through the driver opened at run time.")
        .value("METAL", Backend::Metal, "Metal on the system's default device; Apple only.");
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
          "The status of the CPU, Vulkan, CUDA and Metal backends, in that order; each GPU library "
          "is opened on first use, never at import.");
    m.def("backend_named", &BackendNamed, "name"_a,
          "The backend named 'auto', 'cpu', 'gpu', 'vulkan', 'cuda' or 'metal', or None.");
    m.def("default_backend", &DefaultBackend,
          "The default backend evaluation uses unless told otherwise: what set_default_backend "
          "set, otherwise TERRAINGEN_BACKEND, otherwise CPU.");
    m.def("set_default_backend", &SetDefaultBackend, "backend"_a,
          "Sets the default backend, overriding TERRAINGEN_BACKEND.");
    m.def("resolve_backend", &ResolveBackend, "requested"_a,
          "The backend `requested` resolves to: GPU becomes the best GPU backend available (METAL, "
          "then CUDA, then VULKAN), AUTO the same or CPU when there is none. Raises RuntimeError "
          "for a GPU backend that is unavailable.");
}

}  // namespace terraingen::python
