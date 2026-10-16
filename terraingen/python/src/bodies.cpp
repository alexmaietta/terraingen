/// @file
/// @brief Bindings of the bodies module: body radii and the Moon's and Mars's crater, boulder,
/// chronology and bedform calibrations.
#include "bindings.h"
#include "common.h"

#include "terraingen/bodies/mars/bedforms.h"
#include "terraingen/bodies/mars/craters.h"
#include "terraingen/bodies/mars/production.h"
#include "terraingen/bodies/moon/boulders.h"
#include "terraingen/bodies/moon/chronology.h"
#include "terraingen/bodies/moon/craters.h"
#include "terraingen/bodies/moon/equilibrium.h"
#include "terraingen/bodies/moon/production.h"
#include "terraingen/bodies/moon/specifications.h"
#include "terraingen/bodies/moon/steady_state.h"
#include "terraingen/bodies/moon/surface.h"
#include "terraingen/bodies/radii.h"
#include "terraingen/bodies/surface_model.h"

#include <memory>
#include <utility>

namespace terraingen::python {

namespace {

using SfdPointer = std::shared_ptr<SizeFrequencyDistribution>;

/// @brief A distribution returned by value, held as Python holds distributions.
template <typename T> SfdPointer Shared(T value) {
    return std::make_shared<T>(std::move(value));
}

void BindRadii(nb::module_& m) {
    nb::enum_<Body>(m, "Body", "Bodies with a radius preset.")
        .value("MOON", Body::Moon)
        .value("MARS", Body::Mars)
        .value("MERCURY", Body::Mercury);
    m.def("mean_radius", &MeanRadius, "body"_a,
          "The body's volumetric mean radius in metres (NASA planetary fact sheets).");
    m.def("body_sphere", &BodySphere, "body"_a, "The sphere of the body's mean radius.");
    m.def("body_named", &BodyNamed, "name"_a,
          "The body named 'moon', 'mars' or 'mercury', or None.");
}

void BindLunarDistributions(nb::module_& m) {
    m.def(
        "trask_equilibrium", [] { return Shared(TraskEquilibrium()); },
        "Trask's (1966) lunar crater equilibrium, N(>= D) = 10^-1.1 D^-2 per square metre.");
    m.def(
        "hartmann_equilibrium", [] { return Shared(HartmannEquilibrium()); },
        "Hartmann's lunar crater equilibrium.");
    nb::enum_<MooreTerrain>(m, "MooreTerrain", "Moore's (1971) lunar terrain classes.")
        .value("ROUGH_MARE", MooreTerrain::RoughMare)
        .value("SMOOTH_MARE", MooreTerrain::SmoothMare)
        .value("UPLANDS", MooreTerrain::Uplands)
        .value("HEAVILY_CRATERED", MooreTerrain::HeavilyCratered);
    m.def(
        "moore_distribution",
        [](MooreTerrain terrain) { return Shared(MooreDistribution(terrain)); }, "terrain"_a,
        "Moore's (1971) crater distribution for a lunar terrain class.");
    m.def(
        "viper_specification", [] { return Shared(ViperSpecification()); },
        "The VIPER mission's crater specification.");
    m.def(
        "neukum_production_function", [] { return Shared(NeukumProductionFunction()); },
        "Neukum's lunar production function, per square metre per Gyr at today's rate.");
    m.def(
        "extended_neukum_production_function",
        [] { return Shared(ExtendedNeukumProductionFunction()); },
        "The Neukum production function extended to small craters, as DSNE uses it.");
    m.def("neukum_lunar_chronology", &NeukumLunarChronology, "age_gyr"_a,
          "Neukum's lunar chronology: craters at least 1 km across per square metre on a "
          "surface `age_gyr` old.");
    m.def("neukum_lunar_chronology_rate", &NeukumLunarChronologyRate, "age_gyr"_a,
          "dN1/dT of Neukum's lunar chronology, per square metre per Gyr.");
}

void BindMartianDistributions(nb::module_& m) {
    m.def(
        "ivanov_mars_production_function", [] { return Shared(IvanovMarsProductionFunction()); },
        "Ivanov's (2001) Martian production function.");
    m.def("ivanov_mars_chronology", &IvanovMarsChronology, "age_gyr"_a,
          "Ivanov's (2001) Martian chronology, N(>= 1 km) per square metre.");
    m.def("ivanov_mars_chronology_rate", &IvanovMarsChronologyRate, "age_gyr"_a,
          "dN1/dT of Ivanov's Martian chronology, per square metre per Gyr.");
}

void BindSurfaces(nb::module_& m) {
    nb::class_<SteadyStateAges, CraterAgeModel>(
        m, "SteadyStateAges",
        "Ages of a surface `age_gyr` old in steady state: a crater lasts until the "
        "`equilibrium` density is replaced by `production_rate`, or the surface's age.")
        .def(nb::init<SfdPointer, SfdPointer, double>(), "production_rate"_a, "equilibrium"_a,
             "age_gyr"_a)
        .def_prop_ro("surface_age_gyr", &SteadyStateAges::SurfaceAgeGyr)
        .def("equilibrium_lifetime_gyr", &SteadyStateAges::EquilibriumLifetimeGyr, "diameter"_a);
    nb::class_<SurfaceModel>(m, "SurfaceModel", "A surface's crater density and age model.")
        .def_prop_ro("density", [](const SurfaceModel& s) { return Mutable(s.density); })
        .def_prop_ro("ages", [](const SurfaceModel& s) { return Mutable(s.ages); });
    m.def("lunar_surface", &LunarSurface, "age_gyr"_a,
          "The DSNE lunar surface `age_gyr` old: the extended Neukum production capped at "
          "Trask's equilibrium, valid from 2.5 m to 300 km, with steady-state ages.");
    m.def("mars_surface", &MarsSurface, "age_gyr"_a,
          "A Martian surface `age_gyr` old: Ivanov's chronology and production, valid from 15 m "
          "to 362 km.");
}

void BindCraterShapes(nb::module_& m) {
    m.def("lunar_fresh_crater_morphometry", &LunarFreshCraterMorphometry, "diameter"_a,
          "A fresh lunar crater's depth and rim height ratios.");
    m.def("lunar_fresh_crater_depth_ratio", &LunarFreshCraterDepthRatio, "diameter"_a,
          "A fresh lunar crater's depth over diameter.");
    m.def("mars_fresh_crater_morphometry", &MarsFreshCraterMorphometry, "diameter"_a,
          "A fresh Martian crater's depth and rim height ratios.");
    m.def("lunar_complex_crater_shape", &LunarComplexCraterShape,
          "Lunar complex craters, from 15 km.");
    m.def("mars_complex_crater_shape", &MarsComplexCraterShape,
          "Martian complex craters, from 7 km.");
}

void BindDiffusivities(nb::module_& m) {
    m.def("fassett_thomson_2014_diffusion_age", &FassettThomson2014DiffusionAge, "age_gyr"_a,
          "Fassett and Thomson's (2014) effective diffusion age for a crater `age_gyr` old.");
    m.def("fassett_thomson_2014_diffusivity", &FassettThomson2014Diffusivity,
          "Fassett and Thomson's (2014) constant diffusivity, 5.5 m^2/Myr.");
    m.def("fassett_2022_hartmann_diffusivity", &Fassett2022HartmannDiffusivity,
          "Fassett et al.'s (2022) diffusivity fit with Hartmann's equilibrium.");
    m.def("fassett_2022_trask_diffusivity", &Fassett2022TraskDiffusivity,
          "Fassett et al.'s (2022) diffusivity fit with Trask's equilibrium.");
}

void BindBoulderCalibrations(nb::module_& m) {
    nb::enum_<SpecificationCurve>(m, "SpecificationCurve", "Which SP-8023 boulder curve.")
        .value("MINIMUM", SpecificationCurve::Minimum)
        .value("NOMINAL", SpecificationCurve::Nominal)
        .value("MAXIMUM", SpecificationCurve::Maximum);
    m.def(
        "sp8023_intercrater_blocks",
        [](SpecificationCurve curve) { return SfdPointer(Sp8023IntercraterBlocks(curve)); },
        "curve"_a, "NASA SP-8023 blocks between craters.");
    m.def(
        "sp8023_crater_blocks",
        [](SpecificationCurve curve) { return SfdPointer(Sp8023CraterBlocks(curve)); }, "curve"_a,
        "NASA SP-8023 blocks near craters.");
    nb::enum_<SynthterrainBoulders>(m, "SynthterrainBoulders",
                                    "synthterrain's boulder distributions.")
        .value("INTER_CRATER", SynthterrainBoulders::InterCrater)
        .value("HAWORTH", SynthterrainBoulders::Haworth)
        .value("VIPER_SPECIFICATION", SynthterrainBoulders::ViperSpecification);
    m.def(
        "synthterrain_boulder_distribution",
        [](SynthterrainBoulders boulders) {
            return SfdPointer(SynthterrainBoulderDistribution(boulders));
        },
        "boulders"_a, "One of synthterrain's power-law boulder distributions.");
}

void BindBedformCalibrations(nb::module_& m) {
    m.def("mars_large_ripples", &MarsLargeRipples, "Martian large ripples.");
    m.def("mars_transverse_aeolian_ridges", &MarsTransverseAeolianRidges,
          "Martian transverse aeolian ridges.");
}

}  // namespace

void BindBodies(nb::module_& m) {
    BindRadii(m);
    BindLunarDistributions(m);
    BindMartianDistributions(m);
    BindSurfaces(m);
    BindCraterShapes(m);
    BindDiffusivities(m);
    BindBoulderCalibrations(m);
    BindBedformCalibrations(m);
}

}  // namespace terraingen::python
