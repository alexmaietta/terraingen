// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The field list of material options.
#pragma once

#include "terraingen/layers/layer_fields.h"
#include "terraingen/pipeline/materials.h"
#include "terraingen/util/option_fields.h"

namespace terraingen {

template <typename IO> void Fields(IO& io, MaterialOptions& options) {
    io("exposure", options.exposure,
       "Thresholds marking bare ground as bedrock, read on the ground beneath the first layer that "
       "lays a material; unset for no bedrock.");
    io("dust_cover", options.dustCover, "About the share of the surface under bright dust patches.",
       "", FromBelow(0.0, 1.0));
    io("dust_patch_m", options.dustPatch, "Typical width of a dust patch, in meters.", "m",
       Above(0.0));
    io("seed", options.seed, "Seed of the dust patches.");
}

}  // namespace terraingen
