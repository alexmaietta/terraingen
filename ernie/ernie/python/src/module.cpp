#include "bindings.h"

NB_MODULE(_ernie, m) {
    m.doc() = "Ernie: a standalone Vulkan renderer with a USD-style scene.";
    ernie::python::BindTensor(m);
    ernie::python::BindScene(m);
    ernie::python::BindRender(m);
#ifdef ERNIE_HAS_GUI
    ernie::python::BindGui(m);
#endif
}
