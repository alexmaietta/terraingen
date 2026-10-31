// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Crater and secondary crater layers in a configuration: their densities, ages,
/// degradation and profiles, fields, checks and building.
#pragma once

#include "config/layer_context.h"

#include "terraingen/distribution/distribution_fields.h"
#include "terraingen/distribution/moore.h"
#include "terraingen/distribution/piecewise_power_law.h"
#include "terraingen/distribution/piecewise_uniform.h"
#include "terraingen/distribution/size_frequency.h"
#include "terraingen/layers/craters/crater_fields.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/craters/degradation/diffusion.h"
#include "terraingen/layers/craters/profiles/fassett_thomson.h"
#include "terraingen/layers/craters/secondaries.h"
#include "terraingen/layers/craters/shaping/apply.h"
#include "terraingen/layers/craters/shaping/morphometry.h"
#include "terraingen/random/random_fields.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace terraingen {

/// Density "power_law": `PowerLawSfd`, N per m^2 with D in meters.
struct PowerLawSpec {
    double coefficient = 0.1;
    double exponent = 2.0;
};
/// Density "piecewise_power_law": `PiecewisePowerLawSfd`.
struct PiecewisePowerLawSpec {
    std::vector<PowerLawSegment> segments{PowerLawSegment{}};
};
/// Density "tabulated": `TabulatedSfd` from (D, N(>=D)) rows.
struct TabulatedSpec {
    std::vector<double> diameters;
    std::vector<double> cumulative;
    double diameterUnit = 1.0;  ///< Meters per table diameter unit; 1000 for km.
    double areaUnit = 1.0;      ///< Square meters per table area unit; 1e6 for km^2.
};
/// Density "piecewise_uniform": `PiecewiseUniformSfd`, bands with diameters spread uniformly.
struct PiecewiseUniformSpec {
    std::vector<UniformBand> bands{UniformBand{}};
    double total = 0.0;  ///< Per m^2 across all bands, shared by `density`; 0 for as given.
};
/// Crater density "trask": `TraskEquilibrium`.
struct TraskSpec {};
/// Crater density "hartmann": `HartmannEquilibrium`.
struct HartmannSpec {};
/// Crater density "moore": `MooreDistribution(terrain)`.
struct MooreSpec {
    MooreTerrain terrain = MooreTerrain::RoughMare;
};
/// Crater density "viper": `ViperSpecification`.
struct ViperSpec {};
/// A published crater production function, a `ProductionRate`.
enum class ProductionFunction {
    Neukum,          ///< "neukum": `NeukumProductionFunction`, 10 m to 300 km.
    ExtendedNeukum,  ///< "extended_neukum": `ExtendedNeukumProductionFunction`, 2.5 m to 300 km.
    Ivanov,          ///< "ivanov": `IvanovMarsProductionFunction`, 15 m to 362 km.
};
/// A published crater chronology, N(>=1 km) by surface age.
enum class ChronologyModel {
    Neukum,  ///< "neukum": `NeukumLunarChronology`.
    Ivanov,  ///< "ivanov": `IvanovMarsChronology`.
};
/// Crater density "production_surface": `ProductionSurface`, the craters `production` leaves on a
/// surface `ageGyr` old by `chronology`, with no erosion.
struct ProductionSurfaceSpec {
    ProductionFunction production = ProductionFunction::ExtendedNeukum;
    ChronologyModel chronology = ChronologyModel::Neukum;
    double ageGyr = 3.5;
};
struct MinimumSpec;
/// A crater density, named in JSON by its "type".
using DensitySpec =
    std::variant<PowerLawSpec, PiecewisePowerLawSpec, TabulatedSpec, ProductionSurfaceSpec,
                 MinimumSpec, TraskSpec, HartmannSpec, MooreSpec, ViperSpec, PiecewiseUniformSpec>;
/// Crater density "minimum": `MinimumSfd`, the least of `distributions` at each diameter, such as
/// a production surface capped by an equilibrium.
struct MinimumSpec {
    std::vector<DensitySpec> distributions;
};

/// Crater ages "fresh": `FreshAges`.
struct FreshAgesSpec {};
/// Crater ages "uniform": `UniformAges(lifetimeGyr)`.
struct UniformAgesSpec {
    double lifetimeGyr = 1.0;
};
/// Equilibrium densities for `SteadyStateAgesSpec`.
enum class EquilibriumModel { Trask, Hartmann };
/// Crater ages "steady_state": `SteadyStateAges` from `production`, `equilibrium` and
/// `chronology`.
struct SteadyStateAgesSpec {
    double ageGyr = 3.5;
    ProductionFunction production = ProductionFunction::ExtendedNeukum;
    EquilibriumModel equilibrium = EquilibriumModel::Hartmann;
    ChronologyModel chronology = ChronologyModel::Neukum;
};
/// Crater ages "freshness": `FreshnessAges(freshness, lifetimeGyr)`.
struct FreshnessAgesSpec {
    double lifetimeGyr = 1.0;
    ValueDistribution freshness{ValueDistribution::Kind::Uniform, 0.0, 1.0, 0.0, 1.0};
};
/// A crater age model, named in JSON by its "type".
using AgesSpec =
    std::variant<UniformAgesSpec, FreshAgesSpec, SteadyStateAgesSpec, FreshnessAgesSpec>;

/// Degradation "none": `NoDegradation`, optionally with a fixed depth-to-diameter ratio.
struct NoDegradationSpec {
    std::optional<double> depthRatio;
};
/// Degradation "freshness": `FreshnessDegradation`.
struct FreshnessDegradationSpec {
    double cutoffDepthRatio = 0.04;
};
/// Where "diffusion" degradation takes its diffusion ages from.
enum class DiffusivityModel {
    /// "matched": `EquilibriumDiffusionAges` with steady-state ages, else `Fassett2022Hartmann`.
    Matched,
    Fassett2022Hartmann,  ///< "fassett_2022_hartmann": `Fassett2022HartmannDiffusivity`.
    Fassett2022Trask,     ///< "fassett_2022_trask": `Fassett2022TraskDiffusivity`.
    FassettThomson2014,   ///< "fassett_thomson_2014": `FassettThomson2014Diffusivity`.
    /// "fassett_thomson_2014_calibration": `FassettThomson2014DiffusionAge`.
    FassettThomson2014Calibration,
    PowerLaw,  ///< "power_law": the spec's `powerLaw`.
};
/// Degradation "diffusion": `DiffusionDegradation`, the layer's profile worn by topographic
/// diffusion (`DiffusedProfile`).
struct DiffusionDegradationSpec {
    DiffusivityModel diffusivity = DiffusivityModel::Matched;
    PowerLawDiffusivity powerLaw{};  ///< Used when `diffusivity` is `PowerLaw`.
    double reachRadii = 3.0;         ///< How far the diffused profile reaches, in crater radii.
};
/// A degradation model, named in JSON by its "type".
using DegradationSpec =
    std::variant<FreshnessDegradationSpec, NoDegradationSpec, DiffusionDegradationSpec>;

/// Crater profile "fassett_thomson": `FassettThomsonProfile(rim)`.
struct FassettThomsonSpec {
    FassettThomsonRim rim = FassettThomsonRim::Original;
};
/// Crater profile "martin_parkes_dunstan": `MartinParkesDunstanProfile`.
struct MartinParkesDunstanSpec {};
/// A simple crater profile, named in JSON by its "type".
using SimpleProfileSpec = std::variant<FassettThomsonSpec, MartinParkesDunstanSpec>;
/// Crater profile "complex": `simple` below the simple-to-complex transition of the layer's
/// morphometry, and complex craters, with flat floors and central peaks, from it
/// (`ComplexCraterProfile` with `LunarComplexCraterShape` or `MarsComplexCraterShape`).
struct ComplexProfileSpec {
    SimpleProfileSpec simple{MartinParkesDunstanSpec{}};
};
/// A crater profile, named in JSON by its "type".
using ProfileSpec = std::variant<FassettThomsonSpec, MartinParkesDunstanSpec, ComplexProfileSpec>;

/// The published fits of fresh crater depth, rim height and complex crater shape that a crater
/// layer shapes its craters with.
enum class CraterMorphometryFit {
    Lunar,  ///< "lunar": `LunarFreshCraterMorphometry` and `LunarComplexCraterShape`.
    Mars,   ///< "mars": `MarsFreshCraterMorphometry` and `MarsComplexCraterShape`.
};

/// Gives `CraterLayerOptions::inherited`: `inherited` with the craters of a coarser level read
/// from the crater list `path`.
struct InheritedCratersSpec {
    /// A crater list (`ReadCraterList`), relative to the configuration's folder.
    std::string path{};
    InheritedCraters inherited{};  ///< Its `craters` are left empty; `path` gives them.
};

/// Layer "craters": craters generated over the grid and applied to it (`CraterLayer`).
struct CraterLayerSpec {
    /// The layer's options; the specs below give its manual craters, density map, completeness
    /// and inherited craters, and the secondary layers after it its secondaries.
    CraterLayerOptions options{};
    CraterMorphometryFit morphometry = CraterMorphometryFit::Lunar;
    /// By default a 3.5 Gyr extended Neukum production surface capped by Hartmann equilibrium
    /// (Fassett 2022, https://doi.org/10.1029/2022JE007510).
    DensitySpec density{MinimumSpec{{ProductionSurfaceSpec{}, HartmannSpec{}}}};
    /// Unset gives steady-state ages matching the density (`LayerAges`).
    std::optional<AgesSpec> ages{};
    /// Manual craters, in the surface's coordinates; parsing sets their `manual` flag.
    std::vector<Crater> manual{};
    ProfileSpec profile{MartinParkesDunstanSpec{}};
    /// By default diffusion matched to those ages.
    DegradationSpec degradation{DiffusionDegradationSpec{}};
    std::optional<DensityMapSpec> densityMap{};  ///< Gives `CraterLayerOptions::densityMap`.
    /// Gives `generation.completeness`: where `manual` is a complete catalog, from which diameter.
    std::optional<FieldSpec> completeness{};
    /// Gives `CraterLayerOptions::inherited`.
    std::optional<InheritedCratersSpec> replaceInheritedCraters{};
};

/// Layer "secondaries": secondary craters around the craters of the last crater layer before this
/// one (`SecondaryCraterLayer`), shaped with that layer's profile at a fixed depth ratio and the
/// fresh rim of `morphometry`. `secondaries.onsetCount` must be given.
struct SecondaryLayerSpec {
    SecondaryCraterOptions secondaries{};
    double depthRatio = 0.11;  ///< Depth-to-diameter ratio.
    CraterMorphometryFit morphometry = CraterMorphometryFit::Lunar;
};

template <> struct EnumNames<EquilibriumModel> {
    static constexpr std::array<EnumEntry<EquilibriumModel>, 2> kValues{{
        {"trask", EquilibriumModel::Trask,
         "Trask (1966) lunar equilibrium, N(>=D) = 10^-1.1 D^-2, as the DSNE uses."},
        {"hartmann", EquilibriumModel::Hartmann,
         "Hartmann (1984) lunar equilibrium, N(>=D) = 10^-1.14 D^-1.83 per km^2 with D in km."},
    }};
};

template <> struct EnumNames<ProductionFunction> {
    static constexpr std::array<EnumEntry<ProductionFunction>, 3> kValues{{
        {"neukum", ProductionFunction::Neukum,
         "Neukum et al. (2001) lunar production function, valid from 10 m to 300 km."},
        {"extended_neukum", ProductionFunction::ExtendedNeukum,
         "The Neukum production function continued below 10 m as a power law of slope -2.85, "
         "valid from 2.5 m to 300 km."},
        {"ivanov", ProductionFunction::Ivanov,
         "Ivanov (2001) Martian production function, valid from 15 m to 362 km."},
    }};
};

template <> struct EnumNames<ChronologyModel> {
    static constexpr std::array<EnumEntry<ChronologyModel>, 2> kValues{{
        {"neukum", ChronologyModel::Neukum,
         "Neukum et al. (2001) lunar chronology, for surfaces 0 to 4.6 Gyr old."},
        {"ivanov", ChronologyModel::Ivanov,
         "Ivanov (2001) Martian chronology, for surfaces 0 to 4.6 Gyr old."},
    }};
};

template <> struct EnumNames<DiffusivityModel> {
    static constexpr std::array<EnumEntry<DiffusivityModel>, 6> kValues{{
        {"matched", DiffusivityModel::Matched,
         "With steady-state ages, the diffusivity that wears each crater to Fassett et al.'s "
         "(2022) visibility cutoff, depth 0.04 of the diameter, at the end of its equilibrium "
         "lifetime, as they derive it, for the layer's own profile; fassett_2022_hartmann for "
         "other ages."},
        {"fassett_2022_hartmann", DiffusivityModel::Fassett2022Hartmann,
         "Lunar size-dependent diffusivity of Fassett et al. (2022) for the Hartmann equilibrium, "
         "3.6 D^0.87 m^2/Myr with D in km."},
        {"fassett_2022_trask", DiffusivityModel::Fassett2022Trask,
         "Lunar size-dependent diffusivity of Fassett et al. (2022) for the Trask equilibrium, "
         "3.1 D^1.05 m^2/Myr with D in km."},
        {"fassett_thomson_2014", DiffusivityModel::FassettThomson2014,
         "Lunar diffusivity of Fassett and Thomson (2014), 5.5 m^2/Myr for every crater size; it "
         "erases craters below a few hundred meters within a small share of their lifetimes."},
        {"fassett_thomson_2014_calibration", DiffusivityModel::FassettThomson2014Calibration,
         "Fassett and Thomson's (2014) lunar diffusion age against crater age, independent of "
         "size; suits kilometer-scale craters."},
        {"power_law", DiffusivityModel::PowerLaw,
         "A diffusivity of your own, coefficient (D / 1 km)^exponent m^2/Myr, from the power_law "
         "settings."},
    }};
};

template <> struct EnumNames<CraterMorphometryFit> {
    static constexpr std::array<EnumEntry<CraterMorphometryFit>, 2> kValues{{
        {"lunar", CraterMorphometryFit::Lunar,
         "Fresh lunar craters: Fassett et al. (2022) depth and rim height below 15 km, Pike's "
         "(1977) complex crater fits above."},
        {"mars", CraterMorphometryFit::Mars,
         "Fresh Martian craters: depth and rim height capped for small craters, Garvin et al.'s "
         "(2003) fits for large and complex craters from 7 km."},
    }};
};

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
       "Table diameters in diameter units, strictly increasing; counts "
       "are interpolated linearly in log D and log N between them.",
       "", Above(0.0).MinLength(2));
    io("cumulative", spec.cumulative,
       "Cumulative count N(>=D) at each diameter, per area unit; not increasing.", "", Above(0.0));
    io("diameter_unit", spec.diameterUnit,
       "Meters per table diameter unit: 1 for meters, 1000 for km.", "m/unit", Above(0.0));
    io("area_unit", spec.areaUnit, "Square meters per table area unit: 1 for m^2, 1e6 for km^2.",
       "m^2", Above(0.0));
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
    io("age_gyr", spec.ageGyr, "Age of the surface in Gyr; older surfaces hold more craters.",
       "Gyr", Between(0.0, 4.6));
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
       "Lifetime in Gyr that every crater's age is drawn uniformly within; with "
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
       "Age of the surface in Gyr; it caps every crater's lifetime, so larger "
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
       "Lifetime in Gyr of every crater: freshness 0 is this old, 1 is new; crater "
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
       "Depth over diameter of a crater at the end of its lifetime; lower values "
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
       "Depth over diameter given to every crater; unset keeps each crater's fresh "
       "depth for the body.",
       "", Above(0.0));
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

template <typename IO> void Fields(IO& io, InheritedCratersSpec& spec) {
    io("path", spec.path,
       config::InputPathHelp(
           "Crater list of the coarser level, CSV or JSON as a crater_list output writes it "
           "(with attributes for the realized depths and rims), or a catalog of the craters a DEM "
           "holds"),
       "", Required());
    Fields(io, spec.inherited);
}

template <> struct SpecName<CraterLayerSpec> {
    static constexpr std::string_view kName = "craters";
    static constexpr std::string_view kDescription =
        "Impact craters generated from a density and age model, or placed by hand, and cut into "
        "the terrain.";
};
/// The fields of which craters a crater layer generates: its seed, density, ages and range.
template <typename IO> void CraterDrawFields(IO& io, CraterLayerSpec& craters) {
    io("generate_random", craters.options.generateRandom,
       "Draws random craters from the size distribution; off places only the manual craters.");
    io("seed", craters.options.seed,
       "Seed the craters are generated with; changing it gives a different crater field of the "
       "same statistics.");
    io("density", craters.density,
       "Size-frequency distribution of the generated craters: how many of each diameter per unit "
       "area.");
    io("ages", craters.ages,
       "How generated craters are given ages, which the degradation model wears them down by; "
       "unset gives steady-state ages on the density's production surface and equilibrium.");
    CraterRangeFields(io, craters.options.generation);
}

/// The fields of how a crater layer's craters are shaped and cut into the terrain.
template <typename IO> void CraterShapeFields(IO& io, CraterLayerSpec& craters) {
    io("profile", craters.profile,
       "Shape of a fresh crater's cross-section, scaled to each crater's diameter.");
    io("degradation", craters.degradation,
       "How craters wear down from their fresh shape with age.");
    io("apply", craters.options.apply,
       "How craters are cut into the terrain: their order, datum, blending, fading and irregular "
       "outlines.");
}

/// The fields of the maps, catalogs and source a crater layer reads.
template <typename IO> void CraterInputFields(IO& io, CraterLayerSpec& craters) {
    io("density_map", craters.densityMap,
       "Map that sets the crater density point by point, scaling the density's size distribution "
       "to what it measures; unset keeps the density the same everywhere.");
    io("completeness", craters.completeness,
       "Where the manual craters are a complete catalog: the diameter in meters from which they "
       "stand in for generated craters, infinite or no value where there is no catalog. Unset "
       "keeps every generated crater.");
    SourceResolvesField(io, craters.options.sourceResolvesPixels,
                        "craters (the smaller of this and completeness when both are set)",
                        "diameter");
    io("replace_inherited_craters", craters.replaceInheritedCraters,
       "Craters of a coarser level, such as a coarser run whose heights are the source, stamped "
       "again sharp with their IDs, ages, depths and rims where the source's upsampling softened "
       "them; null for none.");
}

template <typename IO> void Fields(IO& io, CraterLayerSpec& craters) {
    io("morphometry", craters.morphometry,
       "Published fits of fresh crater depth, rim height and complex crater shape the craters "
       "are shaped with.");
    CraterDrawFields(io, craters);
    io("manual", craters.manual,
       "Craters placed by hand in the surface's coordinates, added to any generated ones.");
    CraterShapeFields(io, craters);
    CraterInputFields(io, craters);
}

template <> struct SpecName<SecondaryLayerSpec> {
    static constexpr std::string_view kName = "secondaries";
    static constexpr std::string_view kDescription =
        "Secondary craters around the craters of the last crater layer before this one, after "
        "Singer et al. (2020).";
};
template <typename IO> void Fields(IO& io, SecondaryLayerSpec& spec) {
    Fields(io, spec.secondaries);
    io("depth_ratio", spec.depthRatio,
       "Depth over diameter every secondary is given, with no wear by age; lower values give "
       "shallower secondaries.",
       "", Above(0.0));
    io("morphometry", spec.morphometry,
       "Published fits whose fresh crater rim the secondaries are shaped with.");
}

/// The density, ages, profile and degradation models a crater layer selects.
/// @param path The layer's JSON path, which errors are reported under.
/// @throws ConfigError at `path`.density, .ages, .profile or .degradation for a model whose
/// constructor rejects its parameters.
CraterModels BuildCraterModels(const CraterLayerSpec& craters, const std::string& path = {});

namespace config {

/// The crater production function a configuration names.
std::shared_ptr<const SizeFrequencyDistribution> BuildProduction(ProductionFunction production);

/// The size-frequency distribution a density spec describes.
std::shared_ptr<const SizeFrequencyDistribution> BuildDensity(const DensitySpec& spec);

/// The fresh crater fits `fit` names.
FreshMorphometry BuildMorphometry(CraterMorphometryFit fit);

/// Steady-state ages on the production surface and equilibrium `density` is the minimum of; none
/// for another density.
std::optional<SteadyStateAgesSpec> MatchingAges(const DensitySpec& density);

/// The ages a crater layer gives its craters: its `ages`, or when unset `MatchingAges` of its
/// density, else `SteadyStateAgesSpec`'s.
AgesSpec LayerAges(const CraterLayerSpec& craters);

/// @name Crater layers
/// Checks a crater or secondary layer spec before building, builds it, and checks the crater
/// layer built: its manual shapes and the craters expected over each output.
/// @throws ConfigError under `path` for what the checks or the layer reject.
/// @{
void CheckSpec(const CraterLayerSpec& spec, const std::string& path, LayerChecks& checks);
void CheckSpec(const SecondaryLayerSpec& spec, const std::string& path, LayerChecks& checks);
LayerPtr BuildSpec(const CraterLayerSpec& spec, const std::string& path, const LayerInputs& inputs);
LayerPtr BuildSpec(const SecondaryLayerSpec& spec, const std::string& path,
                   const LayerInputs& inputs);
void CheckBuilt(const CraterLayerSpec& spec, const Layer& built, const std::string& path,
                LayerChecks& checks);
/// @}

}  // namespace config

}  // namespace terraingen
