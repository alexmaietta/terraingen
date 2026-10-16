/// @file
/// @brief The `_terraingen` extension module: each library module's bindings, in dependency
/// order so every type is registered before a default argument needs it.
#include "bindings.h"

NB_MODULE(_terraingen, m) {
    m.doc() = "TerrainGen: scientific terrain generation for planetary surfaces.";
    // Python subclasses (distributions, profiles, layers) held by C++ objects in a script's
    // globals are still referenced when the interpreter finalizes, which nanobind would report as
    // leaks on every exit; the process frees them.
    nanobind::set_leak_warnings(false);
    using namespace terraingen::python;
    BindUtil(m);
    BindRaster(m);
    BindNoise(m);
    BindRandom(m);
    BindGpu(m);
    BindMeshes(m);
    BindDistribution(m);
    BindCraterModels(m);
    BindSources(m);
    BindLayers(m);
    BindCraters(m);
    BindBoulders(m);
    BindFilters(m);
    BindAlbedo(m);
    BindPipeline(m);
}
