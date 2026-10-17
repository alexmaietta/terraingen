/// @file
/// @brief The JSON fields of the material map and its photometry.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/config/fields/boulders.h"
#include "terraingen/config/fields/common.h"

namespace terraingen::config {

template <typename IO> void Fields(IO& io, HapkeParameters& hapke) {
    io("albedo", hapke.albedo, "Single-scattering albedo w, from 0 to 1.");
    io("b", hapke.b, "Asymmetry b of the two-term Henyey-Greenstein phase function, from 0 to 1.");
    io("c", hapke.c,
       "Backscatter fraction c of the two-term Henyey-Greenstein phase function, from 0 to 1.");
    io("roughness", hapke.roughness, "Macroscopic roughness theta-bar, in degrees below 90.",
       "deg");
}

template <typename IO> void Fields(IO& io, PhotometrySpec& photometry) {
    io("reference", photometry.reference,
       "Published Hapke parameters to start from; each class given below replaces its own.");
    MaterialPhotometry& classes = photometry.classes;
    io("soil", classes[static_cast<std::size_t>(MaterialClass::Soil)],
       "Hapke parameters of soil; unset keeps the reference's.");
    io("sand", classes[static_cast<std::size_t>(MaterialClass::Sand)],
       "Hapke parameters of sand; unset keeps the reference's.");
    io("dust", classes[static_cast<std::size_t>(MaterialClass::Dust)],
       "Hapke parameters of dust; unset keeps the reference's.");
    io("bedrock", classes[static_cast<std::size_t>(MaterialClass::Bedrock)],
       "Hapke parameters of bedrock; unset keeps the reference's.");
    io("rock", classes[static_cast<std::size_t>(MaterialClass::Rock)],
       "Hapke parameters of rocks and boulders; unset keeps the reference's.");
}

template <typename IO> void Fields(IO& io, MaterialsSpec& materials) {
    MaterialOptions& options = materials.options;
    io("exposure", options.exposure,
       "Thresholds marking bare ground as bedrock, read on the ground beneath the first layer that "
       "lays a material; unset for no bedrock.");
    io("dust_cover", options.dustCover,
       "About the share of the surface under bright dust patches, from 0 up to 1.");
    io("dust_patch", options.dustPatch, "Typical width of a dust patch, in metres.", "m");
    io("seed", options.seed, "Seed of the dust patches.");
    io("photometry", materials.photometry,
       "Hapke parameters of each material class, which photometry outputs mix.");
}

}  // namespace terraingen::config
