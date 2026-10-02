#include "bindings.h"

NB_MODULE(_terraingen, m) {
    m.doc() = "TerrainGen: scientific terrain generation for the lunar surface.";
    terraingen::python::BindCore(m);
    terraingen::python::BindKernels(m);
    terraingen::python::BindRandom(m);
}
