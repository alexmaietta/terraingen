// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Fitting crater datums to the ground plus the relief of the craters applied before each.
#pragma once

#include "terraingen/layers/craters/shaping/apply.h"
#include "terraingen/layers/craters/shaping/crater_kernel.h"

#include <cstddef>
#include <vector>

namespace terraingen {

/// Shapes craters `fitted` of `prepared` on their slopes (`CraterSlopeResponse`) and fits their
/// datums for a grid: to `sampling.ground`, or without one to `raster` as given, through the
/// datum kernel where it can take them, on a body on a GPU, else through a CPU relief index.
/// @param kernel `MakeCraterShapeKernel` of the models' profile and `options`.
void FitGridDatums(std::vector<PreparedCrater>& prepared, const std::vector<std::size_t>& fitted,
                   const Raster<float>& raster, const CraterShapeModels& models,
                   const CraterShapeKernel& kernel, const CraterApplyOptions& options,
                   const CraterSampling& sampling);

/// `FitGridDatums` for the craters `fitted` reaching scattered points, fitting to
/// `sampling.ground` one crater at a time so each can be kept in `sampling.cache`.
void FitPointDatums(std::vector<PreparedCrater>& prepared, const std::vector<std::size_t>& fitted,
                    const CraterShapeModels& models, const CraterShapeKernel& kernel,
                    const CraterApplyOptions& options, const CraterSampling& sampling);

/// Fits the datums of craters `replacing` of `prepared`, which replace the terrain beneath them
/// (`FitReplacementDatum`), to `sampling.ground` plus the relief of the craters before each, or
/// without a ground to `raster`; at points, keeps each in `sampling.cache` where given.
/// @param raster The grid being shaped, or null for points, which need `sampling.ground`.
void FitReplacementDatums(std::vector<PreparedCrater>& prepared,
                          const std::vector<std::size_t>& replacing, const Raster<float>* raster,
                          const CraterShapeModels& models, const CraterShapeKernel& kernel,
                          const CraterApplyOptions& options, const CraterSampling& sampling);

/// Gives craters `fitted` of `prepared` their datums from `sampling.cache`, as `FitPointDatums`
/// kept them, and their shapes on slopes; false, changing nothing, when any datum is not held.
bool UseCachedDatums(std::vector<PreparedCrater>& prepared, const std::vector<std::size_t>& fitted,
                     const CraterShapeModels& models, const CraterApplyOptions& options,
                     const CraterSampling& sampling);

}  // namespace terraingen
