/// @file
/// @brief Material maps of a terrain: the share of soil, sand, dust, bedrock and rock at each
/// point, and the Hapke photometry they mix to.
#pragma once

#include "terraingen/layers/exposure.h"
#include "terraingen/layers/material.h"
#include "terraingen/pipeline/terrain.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace terraingen {

/// How a terrain's material map is made from its shape and layers.
struct MaterialOptions {
    /// Bedrock where the finished ground is bare (`ExposureField`); none for no bedrock.
    std::optional<ExposureParams> exposure{ExposureParams{}};
    double dustCover = 0.0;   ///< About the share of the surface under dust patches, in [0, 1).
    double dustPatch = 50.0;  ///< Typical width of a dust patch (m).
    std::uint32_t seed = 0;   ///< Varies the dust patches.

    bool operator==(const MaterialOptions&) const = default;
};

/// Checks material options.
/// @throws std::invalid_argument for a dust cover outside [0, 1), a dust patch that is not
/// positive and finite, or invalid exposure thresholds.
void ValidateMaterialOptions(const MaterialOptions& options);

/// The material shares of `terrain` at world (x, y): soil, then bedrock where the finished ground
/// is bare, then dust in patches, then each `MaterialSource` layer's cover in layer order, each
/// read on the ground beneath it.
/// @param spacing Sample spacing in metres; positive.
/// @throws std::invalid_argument as `ValidateMaterialOptions` or `ValidateTerrain`.
MaterialShares MaterialsAt(const Terrain& terrain, const MaterialOptions& options, double x,
                           double y, double spacing);

/// One raster per `MaterialClass` over `grid`, each cell `MaterialsAt` its centre at the grid's
/// spacing (or `evaluation.spacing` where coarser).
/// @throws std::invalid_argument as `MaterialsAt`.
std::vector<Raster<float>> EvaluateMaterials(const Terrain& terrain, const MaterialOptions& options,
                                             const RasterGrid& grid,
                                             const EvaluationOptions& evaluation = {});

/// The Hapke (1993) parameters of a surface, as fitted to Pancam photometry.
struct HapkeParameters {
    double albedo = 0.0;     ///< Single-scattering albedo w, in [0, 1].
    double b = 0.0;          ///< Two-term Henyey-Greenstein asymmetry b, in [0, 1].
    double c = 0.0;          ///< Two-term Henyey-Greenstein backscatter fraction c, in [0, 1].
    double roughness = 0.0;  ///< Macroscopic roughness theta-bar (deg), in [0, 90).

    bool operator==(const HapkeParameters&) const = default;
};

/// Hapke parameters per `MaterialClass`; a class without any cannot be mixed.
using MaterialPhotometry = std::array<std::optional<HapkeParameters>, kMaterialClasses>;

/// Hapke parameters of Martian surfaces at 753 nm from Pancam (Johnson et al. 2006a, b, as
/// tabulated by Fernando et al. 2013): soil from Spirit's landing site soil, sand from
/// Opportunity's Purgatory ripples, dust from its dusty soil and rock from Spirit's dust-free gray
/// rocks. Bedrock has none yet.
MaterialPhotometry MarsPancamPhotometry();

/// Checks photometry parameters.
/// @throws std::invalid_argument for a parameter outside its range.
void ValidateMaterialPhotometry(const MaterialPhotometry& photometry);

/// The Hapke parameters of a mix of materials: each parameter the share-weighted mean of the
/// classes', an areal mix approximation.
/// @throws std::invalid_argument if a class with a share has no parameters.
HapkeParameters MixPhotometry(const MaterialShares& shares, const MaterialPhotometry& photometry);

/// Rasters of the albedo, b, c and roughness that the material rasters `materials` (one per
/// class, as `EvaluateMaterials` gives) mix to.
/// @throws std::invalid_argument for the wrong number of rasters, rasters on different grids, or
/// as `MixPhotometry`.
std::vector<Raster<float>> EvaluatePhotometry(const std::vector<Raster<float>>& materials,
                                              const MaterialPhotometry& photometry);

}  // namespace terraingen
