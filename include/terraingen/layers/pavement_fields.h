// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The field lists of pavement and strata.
#pragma once

#include "terraingen/layers/layer_fields.h"
#include "terraingen/layers/pavement.h"
#include "terraingen/layers/strata.h"
#include "terraingen/util/option_fields.h"

namespace terraingen {

template <typename IO> void Fields(IO& io, PavementOptions& pavement) {
    io("plate_size_m", pavement.plateSize, "Mean plate width, in meters.", "m", Above(0.0));
    io("jitter", pavement.jitter,
       "How far plate centers stray from a square lattice; 0 gives square plates.", "",
       UnitInterval());
    io("warp", pavement.warp, "Bend and roughness of the plate edges, in plate sizes.", "",
       AtLeast(0.0));
    io("crack_width_m", pavement.crackWidth, "Width of the cracks between plates, in meters.", "m",
       Above(0.0));
    io("crack_depth_m", pavement.crackDepth,
       "Depth of the cracks, or height of the ridges when raised, in meters.", "m", AtLeast(0.0));
    io("raised", pavement.raised, "Whether plates are parted by raised ridges instead of cracks.");
    io("edge_rounding_m", pavement.edgeRounding,
       "Width of the rounded shoulder beside each crack, in meters.", "m", AtLeast(0.0));
    io("max_tilt_deg", pavement.maxTiltDegrees, "Largest tilt of a plate, in degrees.", "deg",
       FromBelow(0.0, 90.0));
    io("max_step_m", pavement.maxStep, "Largest height offset of a plate, up or down, in meters.",
       "m", AtLeast(0.0));
    io("octaves", pavement.octaves,
       "Crack octaves: each finer octave splits every plate again, its cracks ending at the "
       "plate's edges.",
       "", Between(1.0, kMaxPavementOctaves));
    io("lacunarity", pavement.lacunarity, "Plate size ratio between octaves.", "", Above(1.0));
    io("gain", pavement.gain,
       "Ratio of crack width, depth, rounding, tilt and step between octaves.", "",
       AboveUpTo(0.0, 1.0));
    io("seed", pavement.seed, "Seed of the plates.");
    ExposureFields(io, pavement.exposure, pavement.exposureSpacing,
                   "Shows plates only where the ground beneath is bare bedrock, fading out over "
                   "soil; unset shows them everywhere.");
    AnchorFields(io, pavement.anchor, "the plates are laid out in");
    FadePixelFields(
        io, pavement.fade,
        "How plates and cracks too fine for the sample spacing fade out, by samples across them.");
}

template <typename IO> void Fields(IO& io, StrataOptions& strata) {
    io("bed_thickness_m", strata.bedThickness, "Mean thickness of a bed, in meters.", "m",
       Above(0.0));
    io("thickness_variation", strata.thicknessVariation,
       "How far each bed boundary strays from even spacing, in bed thicknesses.", "",
       FromBelow(0.0, 1.0));
    io("dip_deg", strata.dipDegrees, "Dip of the beds from horizontal, in degrees.", "deg",
       FromBelow(0.0, 90.0));
    io("dip_azimuth_deg", strata.dipAzimuthDegrees,
       "Direction the beds dip toward, in degrees counter-clockwise from +x (local east on a "
       "body).",
       "deg");
    io("sharpness", strata.sharpness,
       "How sharply each bed is cut into a flat bench and a cliff; 1 leaves the ground as it is.",
       "", AtLeast(1.0));
    io("strength", strata.strength, "Share of the terracing applied.", "", UnitInterval());
    io("cliff_share", strata.cliffShare,
       "Share of each bed's height, at its top, laid as bedrock in the material map.", "",
       AboveUpTo(0.0, 1.0));
    io("seed", strata.seed, "Seed of the bed boundaries.");
    AnchorFields(io, strata.anchor, "the dip is measured in");
    io("hard_share", strata.hardShare,
       "Share of the beds that are hard rock: hard beds form benches and cliffs of bedrock, soft "
       "beds slopes cut at soft_sharpness without bedrock.",
       "", UnitInterval());
    io("soft_sharpness", strata.softSharpness,
       "How sharply soft beds are cut into benches and slopes; 1 leaves them as they are.", "",
       AtLeast(1.0));
}

}  // namespace terraingen
