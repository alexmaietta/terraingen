// SPDX-License-Identifier: BSD-3-Clause

#include "bindings.h"
#include "common.h"
#include "options.h"
#include "trampolines.h"

#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/layers/craters/ages/freshness_ages.h"
#include "terraingen/layers/craters/ages/steady_state.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/degradation/diffusion.h"
#include "terraingen/layers/craters/degradation/diffusivities.h"
#include "terraingen/layers/craters/degradation/freshness_degradation.h"
#include "terraingen/layers/craters/profiles/complex.h"
#include "terraingen/layers/craters/profiles/fassett_thomson.h"
#include "terraingen/layers/craters/profiles/martin_parkes_dunstan.h"
#include "terraingen/layers/craters/shaping/morphometry.h"
#include "terraingen/random/stream.h"
#include "terraingen/random/value_distribution.h"

#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace terraingen::python {

namespace {

/// @brief An age uniform over a crater's lifetime, as Python age models default to.
double UniformAgeGyr(RandomStream& random, double lifetimeGyr) {
    return lifetimeGyr * random.NextDouble();
}

/// @brief A crater age model written in Python: `lifetime_at(diameter)` is required; without
/// `sample_age_at(random, diameter, lifetime_gyr)`, ages are uniform over the lifetime.
class PyCraterAgeModel : public CraterAgeModel {
  public:
    NB_TRAMPOLINE(CraterAgeModel);

  protected:
    double LifetimeAt(double diameter) const override {
        return CallRequiredAs<double>(nb_trampoline, "lifetime_at", diameter);
    }

    double SampleAgeAt(RandomStream& random, double diameter, double lifetimeGyr) const override {
        return CallOverride<double>(nb_trampoline, "sample_age_at",
                                    ByReference<RandomStream>{random}, diameter, lifetimeGyr)
            .value_or(UniformAgeGyr(random, lifetimeGyr));
    }
};

/// @brief A degradation model written in Python: `state(crater)` returns a `CraterState`.
class PyDegradationModel : public DegradationModel {
  public:
    NB_TRAMPOLINE(DegradationModel);

    CraterState State(const Crater& crater) const override {
        return CallRequiredAs<CraterState>(nb_trampoline, "state", crater);
    }
};

/// @brief A crater profile written in Python: `height(distance, diameter, state)` and
/// `reach_radii()` are required; `is_complex(diameter)` is False unless overridden.
class PyCraterProfile : public CraterProfile {
  public:
    NB_TRAMPOLINE(CraterProfile);

    double Height(double distance, double diameter, const CraterState& state) const override {
        return CallRequiredAs<double>(nb_trampoline, "height", distance, diameter, state);
    }

    double ReachRadii() const override {
        return CallRequiredAs<double>(nb_trampoline, "reach_radii");
    }

    bool IsComplex(double diameter) const override {
        NB_OVERRIDE_NAME("is_complex", IsComplex, diameter);
    }
};

void BindAges(nb::module_& m) {
    nb::class_<CraterAgeModel, PyCraterAgeModel>(
        m, "CraterAgeModel",
        "How old craters are: each has a lifetime by size and an age drawn within it. Subclass "
        "it and implement `lifetime_at(diameter)`, and optionally `sample_age_at(random, "
        "diameter, lifetime_gyr)` (uniform over the lifetime otherwise).")
        .def(nb::init<>())
        .def("lifetime_at", &CraterAgeModel::LifetimeGyr, "diameter"_a,
             "The lifetime (Gyr) of a crater `diameter` across; override it.")
        .def(
            "sample_age_at",
            [](const CraterAgeModel& /*model*/, RandomStream& random, double /*diameter*/,
               double lifetimeGyr) { return UniformAgeGyr(random, lifetimeGyr); },
            "random"_a, "diameter"_a, "lifetime_gyr"_a,
            "An age (Gyr) within the lifetime; uniform unless overridden.")
        .def("sample_age_gyr", &CraterAgeModel::SampleAgeGyr, "random"_a, "diameter"_a)
        .def("remaining_lifetime", &CraterAgeModel::RemainingLifetime, "diameter"_a, "age_gyr"_a,
             "The share of a crater's lifetime left: 1 for a new crater, falling to 0 at the end "
             "of its lifetime.");
    nb::class_<FreshAges, CraterAgeModel>(m, "FreshAges", "Every crater is new (age 0).")
        .def(nb::init<>());
    nb::class_<UniformAges, CraterAgeModel>(
        m, "UniformAges", "Ages uniform over a lifetime of `lifetime_gyr`, whatever the size.")
        .def(nb::init<double>(), "lifetime_gyr"_a);
    nb::class_<FreshnessAges, CraterAgeModel>(
        m, "FreshnessAges",
        "Ages from a freshness f drawn per crater from `freshness` (a `ValueDistribution`, also "
        "clamped to [0, 1]): age = (1 - f) * `lifetime_gyr`, whatever the size.")
        .def(nb::init<ValueDistribution, double>(), "freshness"_a, "lifetime_gyr"_a)
        .def_prop_ro("freshness", &FreshnessAges::Freshness)
        .def("sample_freshness", &FreshnessAges::SampleFreshness, "random"_a,
             "Draws a freshness in [0, 1].");
    nb::class_<SteadyStateAges, CraterAgeModel>(
        m, "SteadyStateAges",
        "Ages of a surface `age_gyr` old in steady state: a crater lasts until the "
        "`equilibrium` density is replaced by `production_rate`, or the surface's age, and ages "
        "follow the formation rate of `chronology`.")
        .def(nb::init<std::shared_ptr<SizeFrequencyDistribution>,
                      std::shared_ptr<SizeFrequencyDistribution>, double, Chronology>(),
             "production_rate"_a, "equilibrium"_a, "age_gyr"_a, "chronology"_a)
        .def_prop_ro("surface_age_gyr", &SteadyStateAges::SurfaceAgeGyr)
        .def("equilibrium_lifetime_gyr", &SteadyStateAges::EquilibriumLifetimeGyr, "diameter"_a);
}

void BindCraterState(nb::module_& m) {
    nb::class_<CraterMorphometry>(m, "CraterMorphometry",
                                  "A crater's depth and rim height, each over its diameter.")
        .def(
            "__init__",
            [](CraterMorphometry* self, double depthRatio, double rimHeightRatio) {
                new (self) CraterMorphometry{depthRatio, rimHeightRatio};
            },
            "depth_ratio"_a = 0.0, "rim_height_ratio"_a = 0.0)
        .def_rw("depth_ratio", &CraterMorphometry::depthRatio)
        .def_rw("rim_height_ratio", &CraterMorphometry::rimHeightRatio);
    nb::class_<CraterState>(m, "CraterState",
                            "A crater's shape when fresh and now, and how far diffusion has "
                            "smoothed it.")
        .def(
            "__init__",
            [](CraterState* self, CraterMorphometry fresh, CraterMorphometry current,
               double diffusion) { new (self) CraterState{fresh, current, diffusion}; },
            "fresh"_a = CraterMorphometry{}, "current"_a = CraterMorphometry{}, "diffusion"_a = 0.0)
        .def_rw("fresh", &CraterState::fresh)
        .def_rw("current", &CraterState::current)
        .def_rw("diffusion", &CraterState::diffusion)
        .def_prop_ro("freshness", &CraterState::Freshness,
                     "The depth now over the depth when fresh; 1 for a fresh crater.");
}

void BindDegradationModels(nb::module_& m) {
    nb::class_<DegradationModel, PyDegradationModel>(
        m, "DegradationModel",
        "How a crater's shape changes with age. Subclass it and implement `state(crater)`, "
        "returning a CraterState, for a model of your own.")
        .def(nb::init<>())
        .def("state", &DegradationModel::State, "crater"_a)
        .def("worn_state", &DegradationModel::WornState, "crater"_a, "rate"_a,
             "The state had the crater worn `rate` times as fast as on level ground.");
    nb::class_<NoDegradation, DegradationModel>(
        m, "NoDegradation",
        "Craters keep their fresh shape (`fresh`, a function of diameter giving a "
        "CraterMorphometry, such as `lunar_fresh_crater_morphometry`), or a fixed `depth_ratio`.")
        .def(nb::init<FreshMorphometry, std::optional<double>>(), "fresh"_a,
             "depth_ratio"_a = nb::none());
    nb::class_<FreshnessDegradation, DegradationModel>(
        m, "FreshnessDegradation",
        "Craters shallow from their fresh shape with their freshness under `ages`, down to "
        "`cutoff_depth_ratio`.")
        .def(nb::init<std::shared_ptr<CraterAgeModel>, FreshMorphometry, double>(), "ages"_a,
             "fresh"_a, "cutoff_depth_ratio"_a = 0.04);
}

void BindDiffusion(nb::module_& m) {
    BindOptions<PowerLawDiffusivity>(
        m, "PowerLawDiffusivity",
        "Topographic diffusivity coefficient * D^exponent (m^2 per Myr, D in km), no less than "
        "at `min_diameter` meters; called with (diameter, age_gyr) it gives the diffusion "
        "length squared, in m^2.",
        {&ValidatePowerLawDiffusivity, "validate_power_law_diffusivity", "law"})
        .def("diffusivity", &PowerLawDiffusivity::Diffusivity, "diameter"_a)
        .def("__call__", &PowerLawDiffusivity::operator(), "diameter"_a, "age_gyr"_a);
    nb::class_<DiffusionDegradation, DegradationModel>(
        m, "DiffusionDegradation",
        "Craters smoothed by topographic diffusion over their age (Fassett and Thomson 2014): "
        "`diffusivity` gives each crater's diffusion length squared (m^2) from its diameter and "
        "age, as PowerLawDiffusivity does, and `profile` the diffused shape.")
        .def(
            "__init__",
            [](DiffusionDegradation* self, FreshMorphometry fresh, DiffusionAgeModel diffusivity,
               std::shared_ptr<DiffusedProfile> profile) {
                new (self) DiffusionDegradation(std::move(fresh), std::move(diffusivity),
                                                std::move(profile));
            },
            "fresh"_a, "diffusivity"_a, "profile"_a)
        .def(
            "__init__",
            [](DiffusionDegradation* self, FreshMorphometry fresh,
               const PowerLawDiffusivity& diffusivity, std::shared_ptr<DiffusedProfile> profile) {
                new (self) DiffusionDegradation(std::move(fresh), diffusivity, std::move(profile));
            },
            "fresh"_a, "diffusivity"_a, "profile"_a);
}

void BindSimpleProfiles(nb::module_& m) {
    nb::class_<CraterProfile, PyCraterProfile>(
        m, "CraterProfile",
        "A crater's height relative to the surrounding surface at `distance` meters from its "
        "center. Subclass it and implement `height(distance, diameter, state)` and "
        "`reach_radii()` (how far, in radii, it changes the terrain), and optionally "
        "`is_complex(diameter)`. A Python profile is called for every sample near every crater, "
        "from every evaluation thread, and Python calls take turns on the GIL, so evaluate "
        "terrains with one on one thread (`threads=1`).")
        .def(nb::init<>())
        .def("height", &CraterProfile::Height, "distance"_a, "diameter"_a, "state"_a)
        .def("reach_radii", &CraterProfile::ReachRadii)
        .def("is_complex", &CraterProfile::IsComplex, "diameter"_a);
    BindEnum<FassettThomsonRim>(m, "FassettThomsonRim", "How the Fassett and Thomson rim is fit.");
    nb::class_<FassettThomsonProfile, CraterProfile>(
        m, "FassettThomsonProfile",
        "The fresh simple-crater profile of Fassett and Thomson (2014).")
        .def(nb::init<FassettThomsonRim>(), "rim"_a = FassettThomsonRim::Original);
    nb::class_<MartinParkesDunstanProfile, CraterProfile>(
        m, "MartinParkesDunstanProfile",
        "Martin, Parkes and Dunstan's profile, whose rounded rim lowers as the crater degrades.")
        .def(nb::init<>());
}

void BindComplexShape(nb::module_& m) {
    nb::class_<KilometerPowerLaw>(m, "KilometerPowerLaw",
                                  "coefficient * D^exponent with D in kilometers.")
        .def(
            "__init__",
            [](KilometerPowerLaw* self, double coefficient, double exponent) {
                new (self) KilometerPowerLaw{coefficient, exponent};
            },
            "coefficient"_a = 0.0, "exponent"_a = 0.0)
        .def_rw("coefficient", &KilometerPowerLaw::coefficient)
        .def_rw("exponent", &KilometerPowerLaw::exponent);
    auto shape = nb::class_<ComplexCraterShape>(
        m, "ComplexCraterShape",
        "Where craters turn complex and how their floors and central peaks scale.");
    BindKwargsInit<ComplexCraterShape>(shape);
    shape.def_rw("transition_diameter", &ComplexCraterShape::transitionDiameter)
        .def_rw("floor_diameter", &ComplexCraterShape::floorDiameter)
        .def_rw("peak_height", &ComplexCraterShape::peakHeight)
        .def_rw("peak_volume", &ComplexCraterShape::peakVolume)
        .def_rw("peak_diameter", &ComplexCraterShape::peakDiameter)
        .def_rw("peak_fit_max_diameter", &ComplexCraterShape::peakFitMaxDiameter)
        .def_rw("ejecta_exponent", &ComplexCraterShape::ejectaExponent)
        .def_rw("reach_radii", &ComplexCraterShape::reachRadii);
    m.def("validate_complex_crater_shape", &ValidateComplexCraterShape, "shape"_a, kValidateDoc);
}

void BindCompositeProfiles(nb::module_& m) {
    nb::class_<ComplexCraterProfile, CraterProfile>(
        m, "ComplexCraterProfile",
        "`simple` below `shape.transition_diameter`; above it a flat floor and a central peak.")
        .def(
            "__init__",
            [](ComplexCraterProfile* self, std::shared_ptr<CraterProfile> simple,
               ComplexCraterShape shape) {
                new (self) ComplexCraterProfile(std::move(simple), std::move(shape));
            },
            "simple"_a, "shape"_a)
        .def_prop_ro("shape", &ComplexCraterProfile::Shape);
    nb::class_<DiffusedProfile, CraterProfile>(
        m, "DiffusedProfile",
        "`fresh` smoothed by topographic diffusion, tabulated by the diffusion time over the "
        "diameter squared; used with DiffusionDegradation.")
        .def(
            "__init__",
            [](DiffusedProfile* self, std::shared_ptr<CraterProfile> fresh, double reachRadii,
               std::size_t cells) {
                new (self) DiffusedProfile(std::move(fresh), reachRadii, cells);
            },
            "fresh"_a, "reach_radii"_a = 3.0, "cells"_a = 600)
        .def("age_at_depth_ratio", &DiffusedProfile::AgeAtDepthRatio, "fresh"_a, "diameter"_a,
             "depth_ratio"_a,
             "The diffusion age kappa t / D^2 at which a crater of `fresh` morphometry wears to "
             "`depth_ratio`.");
}

void BindCraterModelSet(nb::module_& m) {
    nb::class_<CraterModels>(m, "CraterModels",
                             "The models a crater layer draws and shapes craters with.")
        .def(
            "__init__",
            [](CraterModels* self, std::shared_ptr<SizeFrequencyDistribution> density,
               std::shared_ptr<CraterAgeModel> ages, std::shared_ptr<DegradationModel> degradation,
               std::shared_ptr<CraterProfile> profile) {
                new (self) CraterModels{std::move(density), std::move(ages), std::move(degradation),
                                        std::move(profile)};
            },
            "density"_a, "ages"_a, "degradation"_a, "profile"_a)
        .def_prop_ro("density", [](const CraterModels& c) { return Mutable(c.density); })
        .def_prop_ro("ages", [](const CraterModels& c) { return Mutable(c.ages); })
        .def_prop_ro("degradation", [](const CraterModels& c) { return Mutable(c.degradation); })
        .def_prop_ro("profile", [](const CraterModels& c) { return Mutable(c.profile); });
}

void BindFreshCraterFits(nb::module_& m) {
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
    m.attr("VISIBILITY_DEPTH_RATIO") = kVisibilityDepthRatio;
    m.def(
        "equilibrium_diffusion_ages",
        [](std::shared_ptr<DiffusedProfile> profile, FreshMorphometry fresh,
           std::function<double(double)> lifetimeGyr, double cutoffDepthRatio) {
            return EquilibriumDiffusionAges(std::move(profile), std::move(fresh),
                                            std::move(lifetimeGyr), cutoffDepthRatio);
        },
        "profile"_a, "fresh"_a, "lifetime_gyr"_a, "cutoff_depth_ratio"_a = kVisibilityDepthRatio,
        "Diffusion ages, a function of (diameter, age_gyr), that wear each crater of `profile` to "
        "`cutoff_depth_ratio` at the end of its lifetime `lifetime_gyr(diameter)`, as Fassett et "
        "al. (2022) derive the diffusivity that holds an equilibrium.");
}

}  // namespace

void BindCraterModels(nb::module_& m) {
    BindAges(m);
    BindCraterState(m);
    BindDegradationModels(m);
    BindSimpleProfiles(m);
    BindComplexShape(m);
    BindCompositeProfiles(m);
    BindDiffusion(m);
    BindFreshCraterFits(m);
    BindDiffusivities(m);
    BindCraterModelSet(m);
}

}  // namespace terraingen::python
