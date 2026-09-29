#pragma once

#include <nanobind/nanobind.h>

namespace terraingen::python {

void BindCore(nanobind::module_& m);
void BindKernels(nanobind::module_& m);

}  // namespace terraingen::python
