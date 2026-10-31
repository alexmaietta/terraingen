// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Material maps of a terrain: the share of soil, sand, dust, bedrock and rock at each
/// point.
#pragma once

#include "terraingen/layers/exposure.h"
#include "terraingen/layers/material.h"
#include "terraingen/pipeline/terrain.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace terraingen {

/// How a terrain's material map is made from its shape and layers.
struct MaterialOptions {
    /// Bedrock where the bare ground, beneath the first layer that lays a material, is bare
    /// (`ExposureField`); none for no bedrock.
    std::optional<ExposureParams> exposure{ExposureParams{}};
    double dustCover = 0.0;   ///< About the share of the surface under dust patches, in [0, 1).
    double dustPatch = 50.0;  ///< Typical width of a dust patch (m).
    std::uint32_t seed = 0;   ///< Varies the dust patches.
};

/// Checks material options.
/// @throws std::invalid_argument for a dust cover outside [0, 1), a dust patch that is not
/// positive and finite, or invalid exposure thresholds.
void ValidateMaterialOptions(const MaterialOptions& options);

/// The material shares of `terrain` at world (x, y): soil, then bedrock where the ground beneath
/// the first `MaterialSource` layer is bare, then dust in patches, then each `MaterialSource`
/// layer's cover in layer order, each read on the ground beneath it.
/// @param spacing Sample spacing in meters; positive.
/// @throws std::invalid_argument as `ValidateMaterialOptions`.
MaterialShares MaterialsAt(const Terrain& terrain, const MaterialOptions& options, double x,
                           double y, double spacing);

/// One raster per `MaterialClass` over `grid`, each cell `MaterialsAt` its center at the grid's
/// spacing (or `evaluation.spacing` where coarser), on `evaluation.threads` threads to the same
/// bits as one. Material layers read the ground beneath them on `evaluation.backend`, and
/// `evaluation.onLayer` is called after each with its index and time; the stage cache is unused.
/// @throws std::invalid_argument as `MaterialsAt`, or for a negative or non-finite spacing.
/// @throws std::runtime_error when a GPU backend is requested but unavailable
/// (`ResolveBackend`).
std::vector<Raster<float>> EvaluateMaterials(const Terrain& terrain, const MaterialOptions& options,
                                             const RasterGrid& grid,
                                             const EvaluationOptions& evaluation = {});

}  // namespace terraingen
