/// @file
/// @brief The JSON fields of crater densities, ages, degradation and profiles.
#pragma once

#include "terraingen/pipeline/config.h"
#include "terraingen/pipeline/fields_common.h"

#include <string_view>

namespace terraingen::config {

template <> struct SpecName<PowerLawSpec> {
    static constexpr std::string_view kName = "power_law";
    static constexpr std::string_view kDescription =
        "A cumulative power law, N(>=D) = coefficient D^-exponent per m^2 with D in metres.";
};
template <typename IO> void Fields(IO& io, PowerLawSpec& spec) {
    io("coefficient", spec.coefficient,
       "Coefficient of N(>=D) = coefficient D^-exponent, the cumulative count per m^2 with D in "
       "metres; scales how many there are.");
    io("exponent", spec.exponent,
       "Exponent of the power law; larger values give relatively more small craters or boulders.");
}

template <typename IO> void Fields(IO& io, PowerLawSegment& segment) {
    io("start", segment.start,
       "Diameter in metres where this segment begins; it runs up to the next segment's start.",
       "m");
    io("coefficient", segment.coefficient,
       "Coefficient of N(>=D) = coefficient D^-exponent, the cumulative count per m^2 with D in "
       "metres.");
    io("exponent", segment.exponent,
       "Exponent of the segment's power law; larger values give relatively more small craters.");
}

template <> struct SpecName<PiecewisePowerLawSpec> {
    static constexpr std::string_view kName = "piecewise_power_law";
    static constexpr std::string_view kDescription =
        "A cumulative distribution made of power-law segments over diameter ranges.";
};
template <typename IO> void Fields(IO& io, PiecewisePowerLawSpec& spec) {
    io("segments", spec.segments,
       "Power-law segments in strictly increasing order of start, each running to the next one's "
       "start; they must meet at their joins.");
}

template <> struct SpecName<TabulatedSpec> {
    static constexpr std::string_view kName = "tabulated";
    static constexpr std::string_view kDescription =
        "A cumulative distribution from a table of diameters and counts N(>=D), in units you "
        "choose.";
};
template <typename IO> void Fields(IO& io, TabulatedSpec& spec) {
    io("diameters", spec.diameters,
       "Table diameters in diameter units, at least two, positive and strictly increasing; counts "
       "are interpolated linearly in log D and log N between them.");
    io("cumulative", spec.cumulative,
       "Cumulative count N(>=D) at each diameter, per area unit; positive and not increasing.");
    io("diameter_unit", spec.diameterUnit,
       "Metres per table diameter unit: 1 for metres, 1000 for km.", "m");
    io("area_unit", spec.areaUnit, "Square metres per table area unit: 1 for m^2, 1e6 for km^2.",
       "m^2");
}

template <> struct SpecName<ProductionSurfaceSpec> {
    static constexpr std::string_view kName = "production_surface";
    static constexpr std::string_view kDescription =
        "The craters a production function leaves on a surface of a given age, scaled by a "
        "chronology, with no erosion; cap it with an equilibrium in a minimum.";
};
template <typename IO> void Fields(IO& io, ProductionSurfaceSpec& spec) {
    io("production", spec.production, "Production function that gives the craters' sizes.");
    io("chronology", spec.chronology,
       "Chronology that gives how many craters at least 1 km across the surface holds.");
    io("age_gyr", spec.ageGyr,
       "Age of the surface in Gyr, from 0 to 4.6; older surfaces hold more craters.", "Gyr");
}

template <> struct SpecName<MinimumSpec> {
    static constexpr std::string_view kName = "minimum";
    static constexpr std::string_view kDescription =
        "The least of several densities at each diameter, such as a production surface capped "
        "by an equilibrium.";
};
template <typename IO> void Fields(IO& io, MinimumSpec& spec) {
    io("distributions", spec.distributions,
       "Densities whose least cumulative count at each diameter is used; at least one.");
}

template <> struct SpecName<TraskSpec> {
    static constexpr std::string_view kName = "trask";
    static constexpr std::string_view kDescription =
        "The Trask (1966) lunar equilibrium density, N(>=D) = 10^-1.1 D^-2.";
};
template <typename IO> void Fields(IO& /*io*/, TraskSpec& /*spec*/) {}

template <> struct SpecName<HartmannSpec> {
    static constexpr std::string_view kName = "hartmann";
    static constexpr std::string_view kDescription =
        "The Hartmann (1984) lunar equilibrium density, N(>=D) = 10^-1.14 D^-1.83 per km^2 with D "
        "in km.";
};
template <typename IO> void Fields(IO& /*io*/, HartmannSpec& /*spec*/) {}

template <> struct SpecName<MooreSpec> {
    static constexpr std::string_view kName = "moore";
    static constexpr std::string_view kDescription =
        "Moore's terrain-specific crater distributions for Lunar Roving Vehicle trafficability.";
};
template <typename IO> void Fields(IO& io, MooreSpec& spec) {
    io("terrain", spec.terrain, "Terrain type whose crater distribution of Moore's to use.");
}

template <> struct SpecName<ViperSpec> {
    static constexpr std::string_view kName = "viper";
    static constexpr std::string_view kDescription =
        "The VIPER environmental specification crater density, as synthterrain uses it; kept for "
        "comparison with synthterrain.";
};
template <typename IO> void Fields(IO& /*io*/, ViperSpec& /*spec*/) {}

template <> struct SpecName<FreshAgesSpec> {
    static constexpr std::string_view kName = "fresh";
    static constexpr std::string_view kDescription =
        "Every crater is fresh, with age and lifetime zero.";
};
template <typename IO> void Fields(IO& /*io*/, FreshAgesSpec& /*spec*/) {}

template <> struct SpecName<UniformAgesSpec> {
    static constexpr std::string_view kName = "uniform";
    static constexpr std::string_view kDescription =
        "Ages uniform over one lifetime for every diameter, so freshness is uniform between 0 "
        "and 1.";
};
template <typename IO> void Fields(IO& io, UniformAgesSpec& spec) {
    io("lifetime_gyr", spec.lifetimeGyr,
       "Lifetime in Gyr, positive, that every crater's age is drawn uniformly within; with "
       "degradation, a crater this old is fully worn.",
       "Gyr");
}

template <> struct SpecName<SteadyStateAgesSpec> {
    static constexpr std::string_view kName = "steady_state";
    static constexpr std::string_view kDescription =
        "Crater ages where small craters are in equilibrium: a crater lasts until the production "
        "rate replaces the equilibrium density, or the surface's age, with ages drawn from the "
        "chronology's formation rate.";
};
template <typename IO> void Fields(IO& io, SteadyStateAgesSpec& spec) {
    io("age_gyr", spec.ageGyr,
       "Age of the surface in Gyr, from 0 to 4.6; it caps every crater's lifetime, so larger "
       "craters on an older surface can be older.",
       "Gyr");
    io("production", spec.production,
       "Production function whose rate, with the equilibrium, sets how long small craters last.");
    io("equilibrium", spec.equilibrium,
       "Equilibrium density that, with the production rate, sets how long small craters last "
       "before they are erased.");
    io("chronology", spec.chronology,
       "Chronology whose formation rate the craters' ages are drawn from.");
}

template <> struct SpecName<FreshnessDegradationSpec> {
    static constexpr std::string_view kName = "freshness";
    static constexpr std::string_view kDescription =
        "Scales depth and rim height linearly with freshness, down to a cutoff depth ratio at the "
        "end of a crater's lifetime.";
};
template <typename IO> void Fields(IO& io, FreshnessDegradationSpec& spec) {
    io("cutoff_depth_ratio", spec.cutoffDepthRatio,
       "Depth over diameter of a crater at the end of its lifetime, in (0, 0.1); lower values "
       "let old craters wear shallower.");
}

template <> struct SpecName<NoDegradationSpec> {
    static constexpr std::string_view kName = "none";
    static constexpr std::string_view kDescription =
        "Craters stay fresh, optionally all with one depth to diameter ratio.";
};
template <typename IO> void Fields(IO& io, NoDegradationSpec& spec) {
    io("depth_ratio", spec.depthRatio,
       "Depth over diameter given to every crater, positive; unset keeps each crater's fresh "
       "depth for the body.");
}

template <typename IO> void Fields(IO& io, PowerLawDiffusivity& law) {
    io("coefficient", law.coefficient,
       "Diffusivity of a 1 km crater, in m^2/Myr, positive; higher values wear craters down "
       "faster.",
       "m^2/Myr");
    io("exponent", law.exponent,
       "Exponent of the diameter in coefficient (D / 1 km)^exponent; 0 gives the same "
       "diffusivity for every crater size.");
    io("min_diameter", law.minDiameter,
       "Diameter in metres below which the diffusivity is held at its value at this diameter; "
       "not negative.",
       "m");
}

template <> struct SpecName<DiffusionDegradationSpec> {
    static constexpr std::string_view kName = "diffusion";
    static constexpr std::string_view kDescription =
        "Wears craters down by topographic diffusion, from each crater's diameter and age.";
};
template <typename IO> void Fields(IO& io, DiffusionDegradationSpec& spec) {
    io("diffusivity", spec.diffusivity,
       "Model of the diffusion age each crater has from its diameter and age; on Mars only "
       "power_law is allowed.");
    io("power_law", spec.powerLaw, "The diffusivity used when the diffusivity model is power_law.");
    io("reach_radii", spec.reachRadii,
       "Distance from the centre, in crater radii, out to which the diffused profile is solved "
       "and held at 0; at least the fresh profile's reach.");
}

template <> struct SpecName<FassettThomsonSpec> {
    static constexpr std::string_view kName = "fassett_thomson";
    static constexpr std::string_view kDescription =
        "Fresh simple-crater profile of Fassett and Thomson (2014), with synthterrain's variants.";
};
template <typename IO> void Fields(IO& io, FassettThomsonSpec& spec) {
    io("rim", spec.rim, "How the profile's interior and exterior fits are joined at the rim.");
}

template <> struct SpecName<MartinParkesDunstanSpec> {
    static constexpr std::string_view kName = "martin_parkes_dunstan";
    static constexpr std::string_view kDescription =
        "Simple-crater profile of Martin, Parkes and Dunstan (2014), developed for the PANGU "
        "simulator; degradation rounds its rim.";
};
template <typename IO> void Fields(IO& /*io*/, MartinParkesDunstanSpec& /*spec*/) {}

template <> struct SpecName<ComplexProfileSpec> {
    static constexpr std::string_view kName = "complex";
    static constexpr std::string_view kDescription =
        "A simple profile below the body's simple-to-complex transition, and complex craters "
        "with flat floors and central peaks above it.";
};
template <typename IO> void Fields(IO& io, ComplexProfileSpec& spec) {
    io("simple", spec.simple,
       "Profile of the simple craters, those below the body's simple-to-complex transition "
       "diameter.");
}

}  // namespace terraingen::config
