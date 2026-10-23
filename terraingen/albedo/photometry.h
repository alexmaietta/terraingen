/// @file
/// @brief Hapke photometry of surface materials, and the mix a material map gives.
#pragma once

#include "terraingen/layers/material.h"
#include "terraingen/raster/raster.h"

#include <array>
#include <optional>
#include <vector>

namespace terraingen {

/// The Hapke (1993) parameters of a surface, as fitted to Pancam photometry.
struct HapkeParameters {
    double albedo = 0.0;     ///< Single-scattering albedo w, in [0, 1].
    double b = 0.0;          ///< Two-term Henyey-Greenstein asymmetry b, in [0, 1].
    double c = 0.0;          ///< Two-term Henyey-Greenstein backscatter fraction c, in [0, 1].
    double roughness = 0.0;  ///< Macroscopic roughness theta-bar (deg), in [0, 90).

    bool operator==(const HapkeParameters& other) const {
        return albedo == other.albedo && b == other.b && c == other.c &&
               roughness == other.roughness;
    }
    bool operator!=(const HapkeParameters& other) const {
        return !(*this == other);
    }
};

/// Hapke parameters per `MaterialClass`; a class without any cannot be mixed.
using MaterialPhotometry = std::array<std::optional<HapkeParameters>, kMaterialClasses>;

/// Hapke parameters of Martian soil, sand, dust, bedrock and rock at 753 nm from Pancam (Johnson
/// et al. 2006a, b; Fernando et al. 2013).
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
