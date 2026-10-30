// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The field lists of distribution segments and bands and the names of the published
/// distributions' variants.
#pragma once

#include "terraingen/distribution/moore.h"
#include "terraingen/distribution/piecewise_uniform.h"
#include "terraingen/distribution/power_law.h"
#include "terraingen/distribution/sp8023.h"
#include "terraingen/distribution/synthterrain.h"
#include "terraingen/util/option_fields.h"

#include <array>

namespace terraingen {

template <> struct EnumNames<MooreTerrain> {
    static constexpr std::array<EnumEntry<MooreTerrain>, 4> kValues{{
        {"rough_mare", MooreTerrain::RoughMare,
         "Moore's rough mare crater distribution: 10^-1 D^-2 per m^2 below 100 m, 10 D^-3 above."},
        {"smooth_mare", MooreTerrain::SmoothMare,
         "Moore's smooth mare crater distribution: 10^-1 D^-2 per m^2 below 40 m, then D^-3, "
         "D^-1.68 from 100 m and D^-3 from 200 m."},
        {"uplands", MooreTerrain::Uplands,
         "Moore's uplands crater distribution: 10^-1 D^-2 per m^2 below 1 km, 10^2 D^-3 above."},
        {"heavily_cratered", MooreTerrain::HeavilyCratered,
         "Moore's heavily cratered distribution: 10^-1 D^-2 per m^2 up to 10 km."},
    }};
};

template <> struct EnumNames<SpecificationCurve> {
    static constexpr std::array<EnumEntry<SpecificationCurve>, 3> kValues{{
        {"minimum", SpecificationCurve::Minimum, "The model's minimum curve: the fewest boulders."},
        {"nominal", SpecificationCurve::Nominal, "The model's nominal curve."},
        {"maximum", SpecificationCurve::Maximum, "The model's maximum curve: the most boulders."},
    }};
};

template <> struct EnumNames<SynthterrainBoulders> {
    static constexpr std::array<EnumEntry<SynthterrainBoulders>, 3> kValues{{
        {"intercrater", SynthterrainBoulders::InterCrater,
         "0.0001 D^-1.75457 per m^2 with D in meters: synthetic_moon's \"low blockiness\"."},
        {"haworth", SynthterrainBoulders::Haworth,
         "0.002 D^-2.6607 per m^2 with D in meters: synthetic_moon's \"high blockiness\"."},
        {"viper_specification", SynthterrainBoulders::ViperSpecification,
         "0.0003 D^-2.482 per m^2 with D in meters: the VIPER Environmental Specification."},
    }};
};

template <typename IO> void Fields(IO& io, PowerLawSegment& segment) {
    io("start_m", segment.start,
       "Diameter in meters where this segment begins; it runs up to the next segment's start.", "m",
       AtLeast(0.0));
    io("coefficient", segment.coefficient,
       "Coefficient of N(>=D) = coefficient D^-exponent, the cumulative count per m^2 with D in "
       "meters.",
       "", Above(0.0));
    io("exponent", segment.exponent,
       "Exponent of the segment's power law; larger values give relatively more small craters.", "",
       Above(0.0));
}

template <typename IO> void Fields(IO& io, UniformBand& band) {
    io("min_m", band.min, "Smallest diameter of the band in meters.", "m", Above(0.0));
    io("max_m", band.max, "Largest diameter of the band in meters, above min_m.", "m", Above(0.0));
    io("density", band.density,
       "Count per m^2 with diameters in the band, spread uniformly across it; a relative weight "
       "when total is set.",
       "1/m^2", AtLeast(0.0));
}

}  // namespace terraingen
