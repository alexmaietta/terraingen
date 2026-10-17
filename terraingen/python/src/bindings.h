/// @file
/// @brief The binding function of each library module, called in dependency order.
#pragma once

#include <nanobind/nanobind.h>

namespace terraingen::python {

void BindUtil(nanobind::module_& m);
void BindRaster(nanobind::module_& m);
void BindNoise(nanobind::module_& m);
void BindRandom(nanobind::module_& m);
void BindGpu(nanobind::module_& m);
void BindMesh(nanobind::module_& m);
void BindDistribution(nanobind::module_& m);
void BindCraterModels(nanobind::module_& m);
void BindSources(nanobind::module_& m);
void BindLayers(nanobind::module_& m);
void BindCraters(nanobind::module_& m);
void BindBoulders(nanobind::module_& m);
void BindAlbedo(nanobind::module_& m);
void BindMaterials(nanobind::module_& m);
void BindPipeline(nanobind::module_& m);

}  // namespace terraingen::python
