// SPDX-License-Identifier: BSD-3-Clause

#include "config/craters.h"

#include "layers/craters/shaping/resolved_state.h"
#include "terraingen/distribution/equilibrium.h"
#include "terraingen/distribution/ivanov.h"
#include "terraingen/distribution/log_polynomial.h"
#include "terraingen/distribution/minimum.h"
#include "terraingen/distribution/neukum.h"
#include "terraingen/distribution/power_law.h"
#include "terraingen/distribution/production_surface.h"
#include "terraingen/distribution/spliced.h"
#include "terraingen/distribution/tabulated.h"
#include "terraingen/distribution/viper.h"
#include "terraingen/io/feature_lists.h"
#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/layers/craters/ages/freshness_ages.h"
#include "terraingen/layers/craters/ages/steady_state.h"
#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/degradation/diffusivities.h"
#include "terraingen/layers/craters/degradation/freshness_degradation.h"
#include "terraingen/layers/craters/profiles/complex.h"
#include "terraingen/layers/craters/profiles/martin_parkes_dunstan.h"
#include "util/overloaded.h"
#include "util/text.h"

#include <utility>

namespace terraingen {

namespace config {

namespace {

using SfdPtr = std::shared_ptr<const SizeFrequencyDistribution>;

Chronology BuildChronology(ChronologyModel chronology) {
    switch (chronology) {
    case ChronologyModel::Ivanov:
        return Chronology(IvanovMarsChronology);
    case ChronologyModel::Neukum:
        break;
    }
    return Chronology(NeukumLunarChronology);
}

SfdPtr BuildEquilibrium(EquilibriumModel equilibrium) {
    switch (equilibrium) {
    case EquilibriumModel::Hartmann:
        return std::make_shared<PowerLawSfd>(HartmannEquilibrium());
    case EquilibriumModel::Trask:
        break;
    }
    return std::make_shared<PowerLawSfd>(TraskEquilibrium());
}

std::shared_ptr<const CraterAgeModel> BuildAges(const AgesSpec& spec) {
    using Result = std::shared_ptr<const CraterAgeModel>;
    return std::visit(
        Overloaded{
            [](const FreshAgesSpec&) -> Result { return std::make_shared<FreshAges>(); },
            [](const UniformAgesSpec& s) -> Result {
                return std::make_shared<UniformAges>(s.lifetimeGyr);
            },
            [](const SteadyStateAgesSpec& s) -> Result {
                return std::make_shared<SteadyStateAges>(BuildProduction(s.production),
                                                         BuildEquilibrium(s.equilibrium), s.ageGyr,
                                                         BuildChronology(s.chronology));
            },
            [](const FreshnessAgesSpec& s) -> Result {
                return std::make_shared<FreshnessAges>(s.freshness, s.lifetimeGyr);
            },
        },
        spec);
}

ComplexCraterShape BuildComplexShape(CraterMorphometryFit fit) {
    switch (fit) {
    case CraterMorphometryFit::Mars:
        return MarsComplexCraterShape();
    case CraterMorphometryFit::Lunar:
        break;
    }
    return LunarComplexCraterShape();
}

/// The diffusion ages of `spec`; `matched` wears craters of steady-state `ages` to the visibility
/// cutoff at the end of their lifetimes, on `profile` from `fresh`.
DiffusionAgeModel BuildDiffusionAges(const DiffusionDegradationSpec& spec,
                                     const std::shared_ptr<const CraterAgeModel>& ages,
                                     const std::shared_ptr<const DiffusedProfile>& profile,
                                     const FreshMorphometry& fresh) {
    switch (spec.diffusivity) {
    case DiffusivityModel::Matched: {
        auto steady = std::dynamic_pointer_cast<const SteadyStateAges>(ages);
        if (!steady) {
            return Fassett2022HartmannDiffusivity();
        }
        return EquilibriumDiffusionAges(profile, fresh, std::move(steady));
    }
    case DiffusivityModel::Fassett2022Hartmann:
        return Fassett2022HartmannDiffusivity();
    case DiffusivityModel::Fassett2022Trask:
        return Fassett2022TraskDiffusivity();
    case DiffusivityModel::FassettThomson2014:
        return FassettThomson2014Diffusivity();
    case DiffusivityModel::FassettThomson2014Calibration:
        return [](double, double ageGyr) { return FassettThomson2014DiffusionAge(ageGyr); };
    case DiffusivityModel::PowerLaw:
        return spec.powerLaw;
    }
    return Fassett2022HartmannDiffusivity();
}

std::shared_ptr<const CraterProfile> BuildProfile(const ProfileSpec& spec,
                                                  CraterMorphometryFit morphometry) {
    using Result = std::shared_ptr<const CraterProfile>;
    const auto simple = [](const SimpleProfileSpec& simpleSpec) {
        return std::visit(Overloaded{
                              [](const FassettThomsonSpec& s) -> Result {
                                  return std::make_shared<FassettThomsonProfile>(s.rim);
                              },
                              [](const MartinParkesDunstanSpec&) -> Result {
                                  return std::make_shared<MartinParkesDunstanProfile>();
                              },
                          },
                          simpleSpec);
    };
    return std::visit(Overloaded{
                          [&](const FassettThomsonSpec& s) -> Result { return simple(s); },
                          [&](const MartinParkesDunstanSpec& s) -> Result { return simple(s); },
                          [&](const ComplexProfileSpec& s) -> Result {
                              return std::make_shared<ComplexCraterProfile>(
                                  simple(s.simple), BuildComplexShape(morphometry));
                          },
                      },
                      spec);
}

/// Sets `models.degradation` from `craters`; diffusion also wears `models.profile`.
void AddDegradation(const CraterLayerSpec& craters, CraterModels& models) {
    const FreshMorphometry fresh = BuildMorphometry(craters.morphometry);
    std::visit(
        Overloaded{
            [&](const FreshnessDegradationSpec& s) {
                models.degradation =
                    std::make_shared<FreshnessDegradation>(models.ages, fresh, s.cutoffDepthRatio);
            },
            [&](const NoDegradationSpec& s) {
                models.degradation = std::make_shared<NoDegradation>(fresh, s.depthRatio);
            },
            [&](const DiffusionDegradationSpec& s) {
                auto profile = std::make_shared<DiffusedProfile>(models.profile, s.reachRadii);
                const DiffusionAgeModel ages = BuildDiffusionAges(s, models.ages, profile, fresh);
                models.degradation = std::make_shared<DiffusionDegradation>(fresh, ages, profile);
                models.profile = std::move(profile);
            },
        },
        craters.degradation);
}

/// The inherited craters a spec describes, their crater list read (`ReadCraterList`).
/// @throws ConfigError at `path`.path for an unreadable or malformed crater list.
InheritedCraters BuildInheritedCraters(const InheritedCratersSpec& spec, const std::string& path,
                                       const std::filesystem::path& directory) {
    InheritedCraters inherited = spec.inherited;
    inherited.craters =
        AtFile(Child(path, "path"), [&] { return ReadCraterList(Resolve(spec.path, directory)); });
    return inherited;
}

/// A length as messages give it, such as 2.5.
std::string Meters(double value) {
    return FormatNumber(value, 6);
}

/// @throws ConfigError at `path` when the density or the ages of a generating layer are not
/// defined at its smallest diameter, as steady-state ages are not below their production
/// function's range.
void CheckModelRange(const CraterLayerSpec& craters, const CraterModels& models,
                     const std::string& path) {
    try {
        models.density->Cumulative(craters.minDiameter);
        models.ages->LifetimeGyr(craters.minDiameter);
    } catch (const std::domain_error&) {
        const DiameterRange& density = models.density->ValidRange();
        std::string message =
            "the density or ages are not defined at " + Meters(craters.minDiameter) + " m";
        const AgesSpec ages = LayerAges(craters);
        if (const auto* steady = std::get_if<SteadyStateAgesSpec>(&ages)) {
            message += "; steady-state ages need the production function's diameters, from " +
                       Meters(BuildProduction(steady->production)->ValidRange().min) + " m";
        } else if (density.min > 0.0) {
            message += "; the density starts at " + Meters(density.min) + " m";
        }
        throw ConfigError(path, message);
    }
}

/// @throws ConfigError at "ages" when steady-state ages name another production function,
/// chronology, surface age or equilibrium than a density of a production surface capped by an
/// equilibrium, so the ages would not follow the craters drawn.
void CheckMatchingAges(const CraterLayerSpec& craters, const std::string& path) {
    const auto* steady = craters.ages ? std::get_if<SteadyStateAgesSpec>(&*craters.ages) : nullptr;
    const std::optional<SteadyStateAgesSpec> matching = MatchingAges(craters.density);
    if (!steady || !matching) {
        return;
    }
    if (matching->production != steady->production || matching->chronology != steady->chronology ||
        matching->ageGyr != steady->ageGyr || matching->equilibrium != steady->equilibrium) {
        throw ConfigError(Child(path, "ages"),
                          "steady-state ages must name the production function, chronology, "
                          "age_gyr and equilibrium of the density's production surface and "
                          "equilibrium, or be left out to follow them");
    }
}

/// @throws ConfigError at a manual crater with a rim height but no depth.
void CheckManualCraters(const std::vector<Crater>& manual, const std::string& path) {
    for (std::size_t n = 0; n < manual.size(); ++n) {
        if (!manual[n].depth && manual[n].rimHeight) {
            throw ConfigError(Child(Element(path, n), "rim_height_m"), "needs a depth");
        }
    }
}

/// The library options a crater layer spec gives, its maps, completeness and inherited craters
/// read and checked.
CraterLayerOptions BuildCraterOptions(const CraterLayerSpec& craters, const std::string& path,
                                      const LayerInputs& inputs) {
    CraterLayerOptions options;
    options.secondaries = inputs.thrown;
    options.generateRandom = craters.generateRandom;
    options.seed = craters.seed;
    options.generation.dMin = craters.minDiameter;
    options.generation.dMax = craters.maxDiameter;
    options.generation.spacing = craters.spacing;
    options.generation.maxCraters = craters.maxCraters;
    for (Crater crater : craters.manual) {
        crater.manual = true;
        options.generation.manual.Append(crater);
    }
    options.apply = craters.apply;
    if (craters.densityMap) {
        options.densityMap =
            BuildDensityMap(*craters.densityMap, Child(path, "density_map"), inputs.directory);
    }
    if (craters.completeness) {
        options.generation.completeness =
            BuildField(*craters.completeness, Child(path, "completeness"), inputs.directory);
    }
    options.sourceResolvesPixels = craters.sourceResolvesPixels;
    if (const auto& inherited = craters.replaceInheritedCraters) {
        options.inherited = BuildInheritedCraters(
            *inherited, Child(path, "replace_inherited_craters"), inputs.directory);
    }
    return options;
}

}  // namespace

std::shared_ptr<const SizeFrequencyDistribution> BuildProduction(ProductionFunction production) {
    switch (production) {
    case ProductionFunction::Neukum:
        return std::make_shared<LogPolynomialSfd>(NeukumProductionFunction());
    case ProductionFunction::Ivanov:
        return std::make_shared<LogPolynomialSfd>(IvanovMarsProductionFunction());
    case ProductionFunction::ExtendedNeukum:
        break;
    }
    return std::make_shared<SplicedSfd>(ExtendedNeukumProductionFunction());
}

std::shared_ptr<const SizeFrequencyDistribution> BuildDensity(const DensitySpec& spec) {
    return std::visit(
        Overloaded{
            [](const ProductionSurfaceSpec& s) -> SfdPtr {
                return std::make_shared<ProductionSurface>(BuildProduction(s.production), s.ageGyr,
                                                           BuildChronology(s.chronology));
            },
            [](const MinimumSpec& s) -> SfdPtr {
                std::vector<SfdPtr> distributions;
                for (const DensitySpec& distribution : s.distributions) {
                    distributions.push_back(BuildDensity(distribution));
                }
                return std::make_shared<MinimumSfd>(std::move(distributions));
            },
            [](const PowerLawSpec& s) -> SfdPtr {
                return std::make_shared<PowerLawSfd>(s.coefficient, s.exponent);
            },
            [](const PiecewisePowerLawSpec& s) -> SfdPtr {
                return std::make_shared<PiecewisePowerLawSfd>(s.segments, SfdKind::Density);
            },
            [](const TabulatedSpec& s) -> SfdPtr {
                return std::make_shared<TabulatedSfd>(s.diameters, s.cumulative, SfdKind::Density,
                                                      s.diameterUnit, s.areaUnit);
            },
            [](const PiecewiseUniformSpec& s) -> SfdPtr {
                return std::make_shared<PiecewiseUniformSfd>(s.bands, SfdKind::Density, s.total);
            },
            [](const TraskSpec&) { return BuildEquilibrium(EquilibriumModel::Trask); },
            [](const HartmannSpec&) { return BuildEquilibrium(EquilibriumModel::Hartmann); },
            [](const MooreSpec& s) -> SfdPtr {
                return std::make_shared<PiecewisePowerLawSfd>(MooreDistribution(s.terrain));
            },
            [](const ViperSpec&) -> SfdPtr {
                return std::make_shared<PiecewisePowerLawSfd>(ViperSpecification());
            },
        },
        spec);
}

FreshMorphometry BuildMorphometry(CraterMorphometryFit fit) {
    switch (fit) {
    case CraterMorphometryFit::Mars:
        return FreshMorphometry(MarsFreshCraterMorphometry);
    case CraterMorphometryFit::Lunar:
        break;
    }
    return FreshMorphometry(LunarFreshCraterMorphometry);
}

std::optional<SteadyStateAgesSpec> MatchingAges(const DensitySpec& density) {
    const auto* minimum = std::get_if<MinimumSpec>(&density);
    if (!minimum) {
        return std::nullopt;
    }
    const ProductionSurfaceSpec* surface = nullptr;
    std::optional<EquilibriumModel> equilibrium;
    for (const DensitySpec& distribution : minimum->distributions) {
        if (const auto* s = std::get_if<ProductionSurfaceSpec>(&distribution)) {
            surface = s;
        } else if (std::holds_alternative<TraskSpec>(distribution)) {
            equilibrium = EquilibriumModel::Trask;
        } else if (std::holds_alternative<HartmannSpec>(distribution)) {
            equilibrium = EquilibriumModel::Hartmann;
        }
    }
    if (!surface || !equilibrium) {
        return std::nullopt;
    }
    return SteadyStateAgesSpec{surface->ageGyr, surface->production, *equilibrium,
                               surface->chronology};
}

AgesSpec LayerAges(const CraterLayerSpec& craters) {
    if (craters.ages) {
        return *craters.ages;
    }
    return MatchingAges(craters.density).value_or(SteadyStateAgesSpec{});
}

void CheckSpec(const CraterLayerSpec& spec, const std::string& path, LayerChecks& checks) {
    CheckManualCraters(spec.manual, Child(path, "manual"));
    At(Child(path, "spacing"), [&] { ValidateCraterSpacing(spec.spacing); });
    At(Child(path, "apply"), [&] { ValidateCraterApplyOptions(spec.apply); });
    const auto* diffusion = std::get_if<DiffusionDegradationSpec>(&spec.degradation);
    if (diffusion && diffusion->diffusivity == DiffusivityModel::PowerLaw) {
        At(Child(path, "degradation"), [&] { ValidatePowerLawDiffusivity(diffusion->powerLaw); });
    }
    CheckMatchingAges(spec, path);
    checks.craters = true;
    if (spec.generateRandom && !(spec.maxDiameter > spec.minDiameter)) {
        throw ConfigError(Child(path, "max_diameter_m"), "must exceed min_diameter_m");
    }
}

void CheckSpec(const SecondaryLayerSpec& spec, const std::string& path, LayerChecks& checks) {
    if (!checks.craters) {
        throw ConfigError(path, "secondaries need a crater layer before this one");
    }
    At(path, [&] { ValidateSecondaryCraterOptions(spec.secondaries); });
}

LayerPtr BuildSpec(const CraterLayerSpec& spec, const std::string& path,
                   const LayerInputs& inputs) {
    CraterLayerOptions options = BuildCraterOptions(spec, path, inputs);
    CraterModels models = BuildCraterModels(spec, path);
    if (spec.generateRandom) {
        CheckModelRange(spec, models, Child(path, "min_diameter_m"));
    }
    return At(path, [&] {
        return std::make_shared<CraterLayer>(std::move(models), std::move(options), inputs.base);
    });
}

LayerPtr BuildSpec(const SecondaryLayerSpec& spec, const std::string& /*path*/,
                   const LayerInputs& inputs) {
    const std::shared_ptr<const CraterLayer>& primaries = inputs.craters.back();
    const CraterShapeModels models{
        primaries->Models().profile,
        std::make_shared<NoDegradation>(BuildMorphometry(spec.morphometry), spec.depthRatio)};
    return std::make_shared<SecondaryCraterLayer>(primaries, models, spec.secondaries,
                                                  CraterApplyOptions{}, inputs.base);
}

void CheckBuilt(const CraterLayerSpec& spec, const Layer& built, const std::string& path,
                LayerChecks& checks) {
    const CraterModels& models = dynamic_cast<const CraterLayer&>(built).Models();
    const std::string manualPath = Child(path, "manual");
    for (std::size_t n = 0; n < spec.manual.size(); ++n) {
        if (spec.manual[n].depth) {
            At(Element(manualPath, n),
               [&] { ResolvedCraterState(models.Shape(), spec.manual[n]); });
        }
    }
    if (!spec.generateRandom) {
        return;
    }
    const std::string minPath = Child(path, "min_diameter_m");
    checks.expectedCraters.resize(checks.extents.size(), 0.0);
    for (std::size_t i = 0; i < checks.extents.size(); ++i) {
        const double expected = At(minPath, [&] {
            return models.density->ExpectedCount(checks.extents[i].area, spec.minDiameter,
                                                 spec.maxDiameter);
        });
        CheckExpectedCount(expected, "craters", "raise min_diameter_m or set max_craters", minPath);
        checks.expectedCraters[i] += expected;
    }
}

}  // namespace config

CraterModels BuildCraterModels(const CraterLayerSpec& craters, const std::string& path) {
    using config::At;
    using config::Child;
    CraterModels models;
    models.density =
        At(Child(path, "density"), [&] { return config::BuildDensity(craters.density); });
    models.ages =
        At(Child(path, "ages"), [&] { return config::BuildAges(config::LayerAges(craters)); });
    models.profile = At(Child(path, "profile"),
                        [&] { return config::BuildProfile(craters.profile, craters.morphometry); });
    At(Child(path, "degradation"), [&] { config::AddDegradation(craters, models); });
    return models;
}

}  // namespace terraingen
