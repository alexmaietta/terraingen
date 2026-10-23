// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The JSON fields of the material map and its photometry.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/config/fields/boulders.h"
#include "terraingen/config/fields/common.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace terraingen::config {

template <typename IO> void Fields(IO& io, HapkeParameters& hapke) {
    io("albedo", hapke.albedo, "Single-scattering albedo w, from 0 to 1.", "", UnitInterval());
    io("b", hapke.b, "Asymmetry b of the two-term Henyey-Greenstein phase function, from 0 to 1.",
       "", UnitInterval());
    io("c", hapke.c,
       "Backscatter fraction c of the two-term Henyey-Greenstein phase function, from 0 to 1.", "",
       UnitInterval());
    io("roughness_deg", hapke.roughness, "Macroscopic roughness theta-bar, in degrees below 90.",
       "deg", FromBelow(0.0, 90.0));
}

/// The JSON key of each material class, in `MaterialClass` order.
inline constexpr std::array<std::string_view, kMaterialClasses> kMaterialClassKeys{
    "soil", "sand", "dust", "bedrock", "rock"};

template <typename IO> void Fields(IO& io, PhotometrySpec& photometry) {
    io("reference", photometry.reference,
       "Published Hapke parameters to start from; each class given below replaces its own.");
    constexpr auto kRock = static_cast<std::size_t>(MaterialClass::Rock);
    for (std::size_t k = 0; k < kMaterialClasses; ++k) {
        const std::string_view what = k == kRock ? "rocks and boulders" : kMaterialClassKeys[k];
        io(kMaterialClassKeys[k], photometry.classes[k],
           "Hapke parameters of " + std::string(what) + "; unset keeps the reference's.");
    }
}

template <typename IO> void Fields(IO& io, MaterialOptions& options) {
    io("exposure", options.exposure,
       "Thresholds marking bare ground as bedrock, read on the ground beneath the first layer that "
       "lays a material; unset for no bedrock.");
    io("dust_cover", options.dustCover,
       "About the share of the surface under bright dust patches, from 0 up to 1.", "",
       FromBelow(0.0, 1.0));
    io("dust_patch", options.dustPatch, "Typical width of a dust patch, in meters.", "m",
       Above(0.0));
    io("seed", options.seed, "Seed of the dust patches.");
}

template <typename IO> void Fields(IO& io, MaterialsSpec& materials) {
    Fields(io, materials.options);
    io("photometry", materials.photometry,
       "Hapke parameters of each material class, which photometry outputs mix.");
}

}  // namespace terraingen::config
