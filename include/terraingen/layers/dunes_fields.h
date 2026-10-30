// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The field lists of dunes, their ripples and grainflows, and the names of dune shapes.
#pragma once

#include "terraingen/layers/dunes.h"
#include "terraingen/layers/layer_fields.h"
#include "terraingen/util/option_fields.h"

#include <array>

namespace terraingen {

template <> struct EnumNames<DuneShape> {
    static constexpr std::array<EnumEntry<DuneShape>, 5> kValues{{
        {"barchan", DuneShape::Barchan,
         "Isolated crescents with a slip face and horns downwind; domes below min_width_m."},
        {"dome", DuneShape::Dome, "Isolated rounded heaps without a slip face."},
        {"transverse", DuneShape::Transverse,
         "Continuous crests across the wind, each with a slip face."},
        {"barchanoid", DuneShape::Barchanoid,
         "Transverse crests bent into linked crescents, as where sand supply rises."},
        {"linear", DuneShape::Linear,
         "Symmetric crests along the wind, as under two winds on scarce sand."},
    }};
};

template <typename IO> void Fields(IO& io, DuneRipples& ripples) {
    io("wavelength_m", ripples.wavelength,
       "Crest-to-crest spacing of the ripples on flat sand, in meters.", "m", Above(0.0));
    io("height_m", ripples.height, "Trough-to-crest height of the ripples on flat sand, in meters.",
       "m", AtLeast(0.0));
    io("lee_fraction", ripples.leeFraction,
       "Share of a ripple wavelength the lee face takes: 0.5 is symmetric, lower values give a "
       "steeper lee face.",
       "", Inside(0.0, 1.0));
    io("sinuosity", ripples.sinuosity,
       "How far a ripple crest wanders along its length, in wavelengths.", "", AtLeast(0.0));
    io("slope_coupling", ripples.slopeCoupling,
       "How strongly the dune's slope turns and tightens the ripples: they shorten on steep lee "
       "faces and turn on the flanks; 0 keeps them straight across the wind.",
       "", AtLeast(0.0));
    io("interdune_cover", ripples.interduneCover,
       "Share of the ripple height kept on the ground between dunes.", "", UnitInterval());
    io("defect_density", ripples.defectDensity,
       "Forked crests (Y-junctions): pairs of crest ends per meter of crest, as measured on dune "
       "fields (Ewing et al. 2010); 0 for none. Mars large ripples: 0.099.",
       "1/m", AtLeast(0.0));
    io("flank_straightening", ripples.flankStraightening,
       "How far crests straighten off the stoss: 1 keeps the sinuosity on flat sand and stoss "
       "slopes and takes it away on flanks and lee slopes, as Martian large ripples do.",
       "", UnitInterval());
    FadePixelFields(
        io, ripples.fade,
        "How ripples too fine for the sample spacing fade out, by samples per wavelength.");
}

template <typename IO> void Fields(IO& io, DuneGrainflows& flows) {
    io("cover", flows.cover, "Chance that each lattice cell of the slip faces holds a grainflow.",
       "", UnitInterval());
    io("min_scarp_width_m", flows.minScarpWidth, "Narrowest scarp, in meters.", "m", Above(0.0));
    io("max_scarp_width_m", flows.maxScarpWidth, "Widest scarp, in meters.", "m", Above(0.0));
    io("min_scarp_length_m", flows.minScarpLength, "Shortest scarp downslope, in meters.", "m",
       Above(0.0));
    io("max_scarp_length_m", flows.maxScarpLength, "Longest scarp downslope, in meters.", "m",
       Above(0.0));
    io("min_lobe_width_m", flows.minLobeWidth, "Narrowest lobe, in meters.", "m", Above(0.0));
    io("max_lobe_width_m", flows.maxLobeWidth, "Widest lobe, in meters.", "m", Above(0.0));
    io("min_lobe_length_m", flows.minLobeLength, "Shortest lobe downslope, in meters.", "m",
       Above(0.0));
    io("max_lobe_length_m", flows.maxLobeLength, "Longest lobe downslope, in meters.", "m",
       Above(0.0));
    io("thickness_m", flows.thickness, "Depth of a scarp and height of a lobe, in meters.", "m",
       AtLeast(0.0));
    FadePixelFields(
        io, flows.fade,
        "How grainflows too narrow for the sample spacing fade out, by samples across a lobe.");
}

/// The fields of every dune's shape and the wind that shapes it.
template <typename IO> void DuneProfileFields(IO& io, DuneOptions& dunes) {
    io("shape", dunes.shape, "The kind of dune.");
    io("wind_azimuth_deg", dunes.windAzimuthDegrees, WindAzimuthHelp("wind", "slip faces face it"),
       "deg");
    io("azimuth_spread_deg", dunes.azimuthSpreadDegrees,
       "Largest difference of each barchan or dome's wind from the azimuth, in degrees.", "deg",
       AtLeast(0.0));
    io("slip_face_angle_deg", dunes.slipFaceAngleDegrees,
       "Slope of the lee slip faces, in degrees: the sand's angle of repose.", "deg",
       Inside(0.0, 90.0));
    io("stoss_slope_deg", dunes.stossSlopeDegrees,
       "Mean slope of the windward faces, in degrees; below the slip face angle.", "deg",
       Inside(0.0, 90.0));
    io("stoss_exponent", dunes.stossExponent,
       "Exponent alpha of the cos^alpha windward profile of dunes; higher is blunter.", "",
       Above(0.0));
    io("dome_exponent", dunes.domeExponent, "Exponent alpha of the cos^alpha profile of domes.", "",
       Above(0.0));
}

/// The fields of isolated barchans and domes, one per lattice cell at most.
template <typename IO> void DuneLatticeFields(IO& io, DuneOptions& dunes) {
    io("lattice_spacing_m", dunes.latticeSpacing,
       "Barchans and domes: side of the lattice cells, each holding at most one dune, in meters.",
       "m", Above(0.0));
    io("density", dunes.density, "Barchans and domes: chance a cell holds a dune.", "",
       UnitInterval());
    io("width_m", dunes.width, "Barchans and domes: mean width, horn to horn, in meters.", "m",
       Above(0.0));
    io("width_spread", dunes.widthSpread,
       "Barchans and domes: each width varies from (1 - spread) to (1 + spread) times the mean.",
       "", FromBelow(0.0, 1.0));
    io("min_width_m", dunes.minWidth,
       "Barchans: width in meters below which a dune is a dome without a slip face, blending "
       "into barchans up to 1.5 times it.",
       "m", AtLeast(0.0));
    io("height_ratio", dunes.heightRatio, "Barchans and domes: height over width.", "", Above(0.0));
    io("horn_length", dunes.hornLength,
       "Barchans: how far the horns reach downwind of the crest, in dune heights.", "",
       AtLeast(0.0));
    io("dome_length", dunes.domeLength, "Domes: length along the wind over width.", "", Above(0.0));
}

/// The fields of fields of transverse, barchanoid and linear crests.
template <typename IO> void DuneCrestFields(IO& io, DuneOptions& dunes) {
    io("wavelength_m", dunes.wavelength,
       "Transverse, barchanoid and linear dunes: crest-to-crest spacing, in meters.", "m",
       Above(0.0));
    io("height_m", dunes.height,
       "Transverse, barchanoid and linear dunes: crest height, in meters.", "m", AtLeast(0.0));
    io("sinuosity", dunes.sinuosity,
       "Transverse, barchanoid and linear dunes: how far a crest wanders along its length, in "
       "wavelengths.",
       "", AtLeast(0.0));
    io("defects", dunes.defects,
       "Transverse, barchanoid and linear dunes: share of the height that varies along crests, "
       "lowering and ending some.",
       "", UnitInterval());
    io("defect_density", dunes.defectDensity,
       "Transverse, barchanoid and linear dunes: pairs of crest ends per meter of crest, so crests "
       "break into segments about 1 / defect_density long; 0 for none. Olympia Undae: 0.0027 "
       "(Ewing et al. 2010).",
       "1/m", AtLeast(0.0));
    io("flank_slope_deg", dunes.flankSlopeDegrees,
       "Linear dunes: mean slope of both flanks, in degrees.", "deg", Inside(0.0, 90.0));
}

template <typename IO> void Fields(IO& io, DuneOptions& dunes) {
    DuneProfileFields(io, dunes);
    DuneLatticeFields(io, dunes);
    DuneCrestFields(io, dunes);
    io("seed", dunes.seed,
       "Seed of the dunes and ripples; changing it gives a different layout of the same "
       "statistics.");
    AnchorFields(io, dunes.anchor,
                 "the dunes are laid out in; they are true to scale near it and absent on the far "
                 "hemisphere");
    FadePixelFields(
        io, dunes.fade,
        "How dunes too fine for the sample spacing fade out, by samples across their width or "
        "wavelength.");
    io("ripples", dunes.ripples, "Ripples superposed on the dunes; unset for none.");
    io("grainflows", dunes.grainflows,
       "Grainflows on the slip faces: scarps below the brink and lobes below them; unset for "
       "none.");
    SourceResolvesField(io, dunes.sourceResolvesPixels, "dunes, their ripples and grainflows",
                        "width or crest wavelength, ripple wavelength and narrowest lobe");
}

}  // namespace terraingen
