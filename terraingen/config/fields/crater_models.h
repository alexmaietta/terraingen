/// @file
/// @brief The JSON fields of crater densities, ages, degradation and profiles.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/config/fields/common.h"

#include <string_view>

namespace terraingen::config {

template <> struct SpecName<PowerLawSpec> {
    static constexpr std::string_view kName = "power_law";
    static constexpr std::string_view kDescription =
        "A cumulative power law, N(>=D) = coefficient D^-exponent per m^2 with D in meters.";
};
template <typename IO> void Fields(IO& io, PowerLawSpec& spec) {
    io("coefficient", spec.coefficient,
       "Coefficient of N(>=D) = coefficient D^-exponent, the cumulative count per m^2 with D in "
       "meters; scales how many there are.",
       "", Above(0.0));
    io("exponent", spec.exponent,
       "Exponent of the power law; larger values give relatively more small craters or boulders.",
       "", Above(0.0));
}

template <typename IO> void Fields(IO& io, PowerLawSegment& segment) {
    io("start", segment.start,
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
       "are interpolated linearly in log D and log N between them.",
       "", Above(0.0));
    io("cumulative", spec.cumulative,
       "Cumulative count N(>=D) at each diameter, per area unit; positive and not increasing.", "",
       Above(0.0));
    io("diameter_unit", spec.diameterUnit,
       "Meters per table diameter unit: 1 for meters, 1000 for km.", "m", Above(0.0));
    io("area_unit", spec.areaUnit, "Square meters per table area unit: 1 for m^2, 1e6 for km^2.",
       "m^2", Above(0.0));
}

template <typename IO> void Fields(IO& io, UniformBand& band) {
    io("min", band.min, "Smallest diameter of the band in meters.", "m", Above(0.0));
    io("max", band.max, "Largest diameter of the band in meters, above min.", "m", Above(0.0));
    io("density", band.density,
       "Count per m^2 with diameters in the band, spread uniformly across it; a relative weight "
       "when total is set.",
       "1/m^2", AtLeast(0.0));
}

template <> struct SpecName<PiecewiseUniformSpec> {
    static constexpr std::string_view kName = "piecewise_uniform";
    static constexpr std::string_view kDescription =
        "Counts per m^2 in diameter bands, each with diameters spread uniformly across it; none "
        "outside the bands.";
};
template <typename IO> void Fields(IO& io, PiecewiseUniformSpec& spec) {
    io("bands", spec.bands,
       "Diameter bands with their counts; overlapping bands add, and gaps between them hold "
       "none.");
    io("total", spec.total,
       "Count per m^2 of all bands together, shared in proportion to each band's density; 0 uses "
       "the densities as given.",
       "1/m^2", AtLeast(0.0));
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
       "Age of the surface in Gyr, from 0 to 4.6; older surfaces hold more craters.", "Gyr",
       Between(0.0, 4.6));
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
       "Gyr", Above(0.0));
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
       "Gyr", Between(0.0, 4.6));
    io("production", spec.production,
       "Production function whose rate, with the equilibrium, sets how long small craters last.");
    io("equilibrium", spec.equilibrium,
       "Equilibrium density that, with the production rate, sets how long small craters last "
       "before they are erased.");
    io("chronology", spec.chronology,
       "Chronology whose formation rate the craters' ages are drawn from.");
}

template <> struct SpecName<FreshnessAgesSpec> {
    static constexpr std::string_view kName = "freshness";
    static constexpr std::string_view kDescription =
        "Ages from a freshness f between 0 and 1 drawn per crater, age = (1 - f) lifetime, with "
        "one lifetime for every diameter; freshness stands for age as in degree-of-freshness "
        "dating (Pohn and Offield 1970; Trang et al. 2015).";
};
template <typename IO> void Fields(IO& io, FreshnessAgesSpec& spec) {
    io("lifetime_gyr", spec.lifetimeGyr,
       "Lifetime in Gyr, positive, of every crater: freshness 0 is this old, 1 is new; crater "
       "albedo such as ray fading reads these ages.",
       "Gyr", Above(0.0));
    io("freshness", spec.freshness,
       "Distribution each crater's freshness is drawn from, also clamped to 0 to 1: 1 is fresh, "
       "0 at the end of its lifetime; with freshness degradation, depth and rim scale with it.");
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
       "let old craters wear shallower.",
       "", Inside(0.0, 0.1));
}

template <> struct SpecName<NoDegradationSpec> {
    static constexpr std::string_view kName = "none";
    static constexpr std::string_view kDescription =
        "Craters stay fresh, optionally all with one depth to diameter ratio.";
};
template <typename IO> void Fields(IO& io, NoDegradationSpec& spec) {
    io("depth_ratio", spec.depthRatio,
       "Depth over diameter given to every crater, positive; unset keeps each crater's fresh "
       "depth for the body.",
       "", Above(0.0));
}

template <typename IO> void Fields(IO& io, PowerLawDiffusivity& law) {
    io("coefficient", law.coefficient,
       "Diffusivity of a 1 km crater, in m^2/Myr, positive; higher values wear craters down "
       "faster.",
       "m^2/Myr", Above(0.0));
    io("exponent", law.exponent,
       "Exponent of the diameter in coefficient (D / 1 km)^exponent; 0 gives the same "
       "diffusivity for every crater size.");
    io("min_diameter", law.minDiameter,
       "Diameter in meters below which the diffusivity is held at its value at this diameter; "
       "not negative.",
       "m", AtLeast(0.0));
}

template <> struct SpecName<DiffusionDegradationSpec> {
    static constexpr std::string_view kName = "diffusion";
    static constexpr std::string_view kDescription =
        "Wears craters down by topographic diffusion, from each crater's diameter and age.";
};
template <typename IO> void Fields(IO& io, DiffusionDegradationSpec& spec) {
    io("diffusivity", spec.diffusivity,
       "Model of the diffusion age each crater has from its diameter and age. The named fits are "
       "lunar; matched follows any steady-state ages, such as Martian ones from Ivanov's "
       "production function.");
    io("power_law", spec.powerLaw, "The diffusivity used when the diffusivity model is power_law.");
    io("reach_radii", spec.reachRadii,
       "Distance from the center, in crater radii, out to which the diffused profile is solved "
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
