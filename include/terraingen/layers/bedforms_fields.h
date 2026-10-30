// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The field lists of bedforms, sand shadows and sand fill.
#pragma once

#include "terraingen/layers/bedforms.h"
#include "terraingen/layers/layer_fields.h"
#include "terraingen/layers/sand_fill.h"
#include "terraingen/layers/sand_shadow.h"
#include "terraingen/util/option_fields.h"

namespace terraingen {

template <typename IO> void Fields(IO& io, BedformOptions& bedforms) {
    io("wavelength_m", bedforms.wavelength,
       "Crest-to-crest spacing, in meters; each field's varies from 0.8 to 1.2 times it.", "m",
       Above(0.0));
    io("height_m", bedforms.height,
       "Trough-to-crest height, in meters, scaled with each field's wavelength.", "m",
       AtLeast(0.0));
    io("wind_azimuth_deg", bedforms.windAzimuthDegrees,
       WindAzimuthHelp("wind", "crests lie across it"), "deg");
    io("azimuth_spread_deg", bedforms.azimuthSpreadDegrees,
       "Largest difference of each field's wind from the azimuth, in degrees.", "deg",
       AtLeast(0.0));
    io("lee_fraction", bedforms.leeFraction,
       "Share of a wavelength the lee face takes: 0.5 is symmetric, lower values give a steeper "
       "lee face.",
       "", Inside(0.0, 1.0));
    io("sinuosity", bedforms.sinuosity,
       "How far a crest wanders along its length, in wavelengths; 0 gives straight crests.", "",
       AtLeast(0.0));
    io("cover", bedforms.cover, "Share of the ground under bedform fields; 0 gives no bedforms.",
       "", Between(0.0, 0.9));
    io("field_size_m", bedforms.fieldSize, "Typical width of a bedform field, in meters.", "m",
       Above(0.0));
    io("seed", bedforms.seed,
       "Seed of the fields and their crests; changing it gives a different layout of the same "
       "statistics.");
    ExposureFields(io, bedforms.exposure, bedforms.exposureSpacing,
                   "Leaves out fields whose centers lie on bedrock, the likelier the barer the "
                   "ground, as sand settles in lows; unset disables it.");
    FadePixelFields(
        io, bedforms.fade,
        "How bedforms too fine for the sample spacing fade out, by samples per wavelength.");
    SourceResolvesField(io, bedforms.sourceResolvesPixels, "bedforms", "wavelength");
}

template <typename IO> void Fields(IO& io, SandShadowOptions& shadows) {
    io("wind_azimuth_deg", shadows.windAzimuthDegrees,
       WindAzimuthHelp("wind", "tails trail away from boulders along it"), "deg");
    io("length_ratio", shadows.lengthRatio,
       "Length of a tail beyond its boulder over the boulder's width; 1.1 is the reattachment "
       "length behind a dome, and modeled shadow dunes run 1.3 to 5.6, longer in lighter "
       "winds.",
       "", Above(0.0));
    io("flank_angle_deg", shadows.flankAngleDegrees,
       "Slope of a tail's flanks, in degrees, at most the angle of repose (about 32 for dry "
       "sand); sets the tail's height with the boulder's width.",
       "deg", Inside(0.0, 90.0));
    io("min_diameter_m", shadows.minDiameter, "Smallest boulder with a tail, in meters.", "m",
       AtLeast(0.0));
    io("cover_thickness_m", shadows.coverThickness,
       "Sand depth that fully covers the ground in the material map, in meters.", "m", Above(0.0));
    FadePixelFields(
        io, shadows.fade,
        "How tails behind boulders too small for the sample spacing fade out, by samples across "
        "the boulder.");
}

template <typename IO> void Fields(IO& io, SandFillOptions& fill) {
    io("reach_m", fill.reach,
       "Radius in meters of the surroundings a low is measured against: two rings, at half the "
       "reach and at the reach.",
       "m", Above(0.0));
    io("fill", fill.fill, "Share of a low's depth below its surroundings that sand fills.", "",
       UnitInterval());
    io("max_thickness_m", fill.maxThickness,
       "The thickest the sand gets, in meters, approached smoothly.", "m", Above(0.0));
    io("cover_thickness_m", fill.coverThickness,
       "Sand thickness in meters from which it fully covers the ground in the material map.", "m",
       Above(0.0));
}

}  // namespace terraingen
