#include "bindings.h"

NB_MODULE(_terraingen, m) {
    m.doc() = "TerrainGen: scientific terrain generation for the lunar surface.";
    // Python subclasses (distributions, profiles, layers) held by C++ objects in a script's
    // globals are still referenced when the interpreter finalizes, which nanobind would report as
    // leaks on every exit; the process frees them.
    nanobind::set_leak_warnings(false);
    terraingen::python::BindCore(m);
    terraingen::python::BindKernels(m);
    terraingen::python::BindLog(m);
    terraingen::python::BindPipeline(m);
    terraingen::python::BindModels(m);
    terraingen::python::BindTerrain(m);
    terraingen::python::BindAlbedo(m);
    terraingen::python::BindRandom(m);
}
