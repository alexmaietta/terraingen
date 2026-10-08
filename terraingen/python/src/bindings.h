#pragma once

#include <nanobind/nanobind.h>

namespace terraingen::python {

void BindCore(nanobind::module_& m);
void BindKernels(nanobind::module_& m);
void BindLog(nanobind::module_& m);
void BindPipeline(nanobind::module_& m);
void BindRandom(nanobind::module_& m);
void BindGeometry(nanobind::module_& m);
void BindModels(nanobind::module_& m);
void BindTerrain(nanobind::module_& m);
void BindAlbedo(nanobind::module_& m);
void BindRasterIo(nanobind::module_& m);
void BindFeatures(nanobind::module_& m);
void BindGpu(nanobind::module_& m);

}  // namespace terraingen::python
