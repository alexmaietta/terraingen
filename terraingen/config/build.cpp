// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/config/build.h"

#include "terraingen/albedo/crater_albedo.h"
#include "terraingen/albedo/raster_albedo.h"
#include "terraingen/albedo/rock_albedo.h"
#include "terraingen/albedo/slope_albedo.h"
#include "terraingen/albedo/synthetic.h"
#include "terraingen/config/fields/checks.h"
#include "terraingen/config/fields/fields.h"
#include "terraingen/config/fields/ranges.h"
#include "terraingen/config/generate.h"
#include "terraingen/config/validate.h"
#include "terraingen/distribution/equilibrium.h"
#include "terraingen/distribution/ivanov.h"
#include "terraingen/distribution/log_polynomial.h"
#include "terraingen/distribution/minimum.h"
#include "terraingen/distribution/neukum.h"
#include "terraingen/distribution/power_law.h"
#include "terraingen/distribution/production_surface.h"
#include "terraingen/distribution/spliced.h"
#include "terraingen/distribution/tabulated.h"
#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/layers/craters/ages/freshness_ages.h"
#include "terraingen/layers/craters/ages/steady_state.h"
#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/degradation/diffusivities.h"
#include "terraingen/layers/craters/degradation/freshness_degradation.h"
#include "terraingen/layers/craters/profiles/martin_parkes_dunstan.h"
#include "terraingen/layers/craters/shaping/morphometry.h"
#include "terraingen/layers/noise_relief.h"
#include "terraingen/layers/transforms.h"
#include "terraingen/mesh/io/icq.h"
#include "terraingen/mesh/io/mesh_io.h"
#include "terraingen/mesh/shape_model.h"
#include "terraingen/sources/field.h"
#include "terraingen/sources/geotiff_source.h"
#include "terraingen/sources/source_chain.h"
#include "terraingen/sources/synthetic_source.h"
#include "terraingen/sources/upsampling.h"
#include "terraingen/util/overloaded.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace terraingen {

namespace {

using config::At;
using config::AtFile;
using config::BuildBoulderOptions;
using config::BuildProduction;
using config::BuildRegionShape;
using config::Child;
using config::Element;
using config::Resolve;
using SfdPtr = std::shared_ptr<const SizeFrequencyDistribution>;
using LayerPtr = std::shared_ptr<const Layer>;

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

SfdPtr BuildDensity(const DensitySpec& spec) {
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
                const double start = s.segments.empty() ? 0.0 : s.segments.front().start;
                return std::make_shared<PiecewisePowerLawSfd>(s.segments, SfdKind::Density,
                                                              DiameterRange{start});
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

FreshMorphometry BuildMorphometry(CraterMorphometryFit fit) {
    switch (fit) {
    case CraterMorphometryFit::Mars:
        return FreshMorphometry(MarsFreshCraterMorphometry);
    case CraterMorphometryFit::Lunar:
        break;
    }
    return FreshMorphometry(LunarFreshCraterMorphometry);
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

/// The diffusion ages of `spec`; `matched` wears craters of steady-state `ages` to the
/// visibility cutoff at the end of their lifetimes, on `profile` from `fresh`.
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

/// The distribution a boulder size spec names, or null for the layer's Golombek-Rapp boulders.
std::shared_ptr<SizeFrequencyDistribution> BuildBoulderSizes(const BoulderSizesSpec& spec) {
    using Result = std::shared_ptr<SizeFrequencyDistribution>;
    const auto density = [](const DensitySpec& named) {
        return std::const_pointer_cast<SizeFrequencyDistribution>(BuildDensity(named));
    };
    return std::visit(
        Overloaded{
            [](const GolombekRappBouldersSpec&) -> Result { return nullptr; },
            [](const Sp8023IntercraterBouldersSpec& s) -> Result {
                return std::make_shared<TabulatedSfd>(Sp8023IntercraterBlocks(s.curve));
            },
            [](const Sp8023CraterBouldersSpec& s) -> Result {
                return std::make_shared<TabulatedSfd>(Sp8023CraterBlocks(s.curve));
            },
            [](const SynthterrainBouldersSpec& s) -> Result {
                return std::make_shared<PowerLawSfd>(SynthterrainBoulderDistribution(s.model));
            },
            [&](const PowerLawSpec& s) -> Result { return density(s); },
            [&](const PiecewisePowerLawSpec& s) -> Result { return density(s); },
            [&](const TabulatedSpec& s) -> Result { return density(s); },
            [&](const PiecewiseUniformSpec& s) -> Result { return density(s); },
        },
        spec);
}

/// The upsampler a spec names, its lengths in the surface's units.
/// @param path The spec's JSON path, which errors are reported under.
/// @throws ConfigError at `path` for settings a registered type rejects.
std::shared_ptr<const Upsampler> BuildUpsampler(const UpsamplingSpec& spec, const Surface& surface,
                                                const std::string& path) {
    if (const std::optional<ResampleMethod> method = KernelUpsamplerMethod(spec.type)) {
        return std::make_shared<KernelUpsampler>(*method);
    }
    if (spec.type == "diamond_square") {
        DiamondSquareOptions options = spec.diamondSquare;
        options.minPixelSize /= surface.UnitLength();
        return At(path, [&] { return std::make_shared<DiamondSquareUpsampler>(options); });
    }
    return At(path, [&] {
        return MakeRegisteredUpsampler(spec.type, spec.settings.empty() ? "{}" : spec.settings);
    });
}

/// The source a spec describes, its lengths in the surface's units.
/// @param path The spec's JSON path, which errors are reported under.
/// @throws ConfigError at `path`, or below it, for what the sources reject.
std::shared_ptr<const Source> BuildSource(const SourceSpec& spec,
                                          const std::filesystem::path& directory,
                                          const Surface& surface, const std::string& path) {
    using Result = std::shared_ptr<const Source>;
    const double unit = surface.UnitLength();
    return std::visit(
        Overloaded{
            [&](const RasterSourceSpec& s) -> Result {
                GeoTiffSourceOptions read = s.read;
                if (read.upsampler) {
                    throw ConfigError(Child(path, "upsampling"),
                                      "gives the raster's upsampler; leave read.upsampler unset");
                }
                if (read.effectivePixelSize) {
                    *read.effectivePixelSize /= unit;
                }
                read.upsampler = BuildUpsampler(s.upsampling, surface, Child(path, "upsampling"));
                return At(path, [&] {
                    return std::make_shared<GeoTiffSource>(Resolve(s.path, directory), read);
                });
            },
            [&](const ChainSourceSpec& s) -> Result {
                std::vector<Result> sources;
                for (std::size_t n = 0; n < s.sources.size(); ++n) {
                    sources.push_back(BuildSource(s.sources[n], directory, surface,
                                                  Element(Child(path, "sources"), n)));
                }
                return At(path, [&] { return std::make_shared<SourceChain>(std::move(sources)); });
            },
            [&](const SyntheticSourceSpec& s) -> Result {
                std::optional<double> pixelSize = s.pixelSize;
                if (pixelSize) {
                    *pixelSize /= unit;
                }
                const std::shared_ptr<const Upsampler> upsampler =
                    s.upsampling ? BuildUpsampler(*s.upsampling, surface, Child(path, "upsampling"))
                                 : nullptr;
                return At(path, [&] {
                    return std::make_shared<SyntheticSource>(StackParams(s.stack),
                                                             static_cast<float>(s.elevation),
                                                             pixelSize, upsampler, surface);
                });
            },
        },
        spec);
}

/// A source in place of another, the same but for its resolution, which is infinite everywhere:
/// synthetic heights hold no measured features.
class UnmeasuredSource final : public Source {
  public:
    using Source::Fill;

    explicit UnmeasuredSource(std::shared_ptr<const Source> source) : source_(std::move(source)) {}

    RasterGrid Grid() const override {
        return source_->Grid();
    }
    Vec2d ReferencePoint() const override {
        return source_->ReferencePoint();
    }
    GeoKeys Keys() const override {
        return source_->Keys();
    }
    void Fill(Raster<float>& heights, double spacing) const override {
        source_->Fill(heights, spacing);
    }
    float HeightAt(double x, double y, double spacing) const override {
        return source_->HeightAt(x, y, spacing);
    }

  private:
    std::shared_ptr<const Source> source_;
};

/// The source whose resolution layers read for what it already holds (`sourceResolvesPixels`):
/// `built` itself for a raster, null for a synthetic source, and for a chain the chain with its
/// synthetic sources resolving nothing, or null when all of them are synthetic.
/// @param built The source `BuildSource` built from `spec`.
std::shared_ptr<const Source> MeasuredSource(const SourceSpec& spec,
                                             const std::shared_ptr<const Source>& built) {
    using Result = std::shared_ptr<const Source>;
    return std::visit(
        Overloaded{
            [&](const RasterSourceSpec&) -> Result { return built; },
            [](const SyntheticSourceSpec&) -> Result { return nullptr; },
            [&](const ChainSourceSpec& s) -> Result {
                const auto& members = static_cast<const SourceChain&>(*built).Sources();
                std::vector<Result> sources;
                bool measured = false;
                for (std::size_t n = 0; n < s.sources.size(); ++n) {
                    Result member = MeasuredSource(s.sources[n], members[n]);
                    measured = measured || member != nullptr;
                    sources.push_back(member ? std::move(member)
                                             : std::make_shared<UnmeasuredSource>(members[n]));
                }
                return measured ? std::make_shared<SourceChain>(std::move(sources)) : nullptr;
            },
        },
        spec);
}

/// The point noise is measured from: the source's reference point, or the origin without one.
Vec2d ReferencePoint(const TerrainGenerator& generator) {
    return generator.source ? generator.source->ReferencePoint() : Vec2d{};
}

/// What a layer is built on: the source's reference point, the top-level crater layers so far (all
/// for boulder ejecta, the last for secondaries), the last boulder layer (for sand shadows) and the
/// surface.
struct LayerInputs {
    Vec2d referencePoint;
    const std::filesystem::path& directory;
    const std::vector<std::shared_ptr<const CraterLayer>>& craters;
    const std::shared_ptr<const BoulderLayer>& boulders;
    const Surface& surface;
    /// The source the configuration gives; null without one.
    const std::shared_ptr<const Source>& source;
    /// The source's measured data that layers leave out what it resolves of
    /// (`MeasuredSource`); null for none.
    const std::shared_ptr<const Source>& measured;
    /// The secondaries later layers throw from a crater layer built with these inputs.
    std::vector<SecondaryCraterOptions> thrown{};
};

LayerPtr BuildLayer(const LayerSpec& spec, const std::string& path, const LayerInputs& context);

/// The roughness of the source at its native resolution (`EstimateSourceRoughness`).
/// @throws std::invalid_argument without a source, or as `EstimateSourceRoughness`.
BaseRoughness SourceRoughness(const LayerInputs& context) {
    if (!context.source) {
        throw std::invalid_argument("Estimating from the source needs a source");
    }
    return EstimateSourceRoughness(*context.source, context.surface);
}

LayerPtr BuildLayerSpec(const NoiseLayerSpec& s, const std::string& path,
                        const LayerInputs& context) {
    const SyntheticTerrainParams params = At(path, [&] {
        return s.estimateFromSource ? s.EstimatedParams(SourceRoughness(context)) : s.Params();
    });
    return At(path, [&] {
        return std::make_shared<NoiseRelief>(params.layers, context.referencePoint, context.surface,
                                             context.measured, s.sourceResolvesPixels);
    });
}

LayerPtr BuildLayerSpec(const CraterLayerSpec& craters, const std::string& path,
                        const LayerInputs& context) {
    CraterLayerOptions options;
    options.secondaries = context.thrown;
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
    options.surface = context.surface;
    if (craters.densityMap) {
        options.densityMap = config::BuildDensityMap(*craters.densityMap,
                                                     Child(path, "density_map"), context.directory);
    }
    if (craters.completeness) {
        options.generation.completeness = config::BuildField(
            *craters.completeness, Child(path, "completeness"), context.directory);
    }
    options.sourceResolvesPixels = craters.sourceResolvesPixels;
    CraterModels models = BuildCraterModels(craters, path);
    return At(path, [&] {
        return std::make_shared<CraterLayer>(std::move(models), std::move(options),
                                             context.measured);
    });
}

LayerPtr BuildLayerSpec(const BoulderLayerSpec& s, const std::string& path,
                        const LayerInputs& context) {
    BoulderLayerOptions options = BuildBoulderOptions(s, path, context.directory);
    const bool throws = options.generateRandom && options.ejecta.MinCraterDiameter() > 0.0;
    return std::make_shared<BoulderLayer>(
        std::move(options),
        throws ? context.craters : std::vector<std::shared_ptr<const CraterLayer>>{},
        context.surface, context.measured);
}

LayerPtr BuildLayerSpec(const RoughnessLayerSpec& s, const std::string& path,
                        const LayerInputs& context) {
    const RoughnessOptions options = At(path, [&] {
        return s.estimateFromSource ? s.EstimatedOptions(SourceRoughness(context)) : s.Options();
    });
    return std::make_shared<RoughnessLayer>(options, context.surface);
}

/// A spectral continuation layer, fitted to the source wherever the spec gives no slope or
/// amplitude; a valid configuration has a source whenever the spec leaves one out.
LayerPtr BuildLayerSpec(const SpectralContinuationLayerSpec& spec, const std::string& path,
                        const LayerInputs& context) {
    const Surface& surface = context.surface;
    SpectralContinuationOptions options = spec.Options();
    const RasterGrid grid = context.source ? context.source->Grid() : RasterGrid{};
    options.pixelSize = spec.pixelSize.value_or(grid.PixelSize() * surface.UnitLength());
    if (!(spec.slope && spec.octaveRms)) {
        const PowerLawSpectrum fit = At(path, [&] {
            const Raster<float> heights =
                SampleSourceWindow(*context.source, options.pixelSize / surface.UnitLength(),
                                   grid.transform.PixelToWorld(0.5, 0.5));
            return EstimatePowerLawSpectrum(heights, spec.fit);
        });
        options.slope = spec.slope.value_or(fit.slope);
        options.octaveRms = spec.octaveRms.value_or(fit.octaveRms);
    }
    return std::make_shared<SpectralContinuationLayer>(options, surface);
}

LayerPtr BuildLayerSpec(const SecondaryLayerSpec& spec, const std::string& /*path*/,
                        const LayerInputs& context) {
    const std::shared_ptr<const CraterLayer>& primaries = context.craters.back();
    const CraterShapeModels models{
        primaries->Models().profile,
        std::make_shared<NoDegradation>(BuildMorphometry(spec.morphometry), spec.depthRatio)};
    return std::make_shared<SecondaryCraterLayer>(primaries, models, spec.secondaries,
                                                  CraterApplyOptions{}, context.measured);
}

LayerPtr BuildLayerSpec(const MaskedLayerSpec& s, const std::string& path,
                        const LayerInputs& context) {
    return std::make_shared<MaskedLayer>(BuildLayer(*s.layer, Child(path, "layer"), context),
                                         BuildRegionShape(s.shape), s.feather, s.blend,
                                         context.surface);
}

LayerPtr BuildLayerSpec(const DuneLayerSpec& s, const std::string&, const LayerInputs& context) {
    return std::make_shared<DuneLayer>(s.dunes, context.surface, context.measured);
}

LayerPtr BuildLayerSpec(const BedformLayerSpec& s, const std::string&, const LayerInputs& context) {
    return std::make_shared<BedformLayer>(s.bedforms, context.surface, context.measured);
}

LayerPtr BuildLayerSpec(const PavementLayerSpec& s, const std::string&,
                        const LayerInputs& context) {
    return std::make_shared<PavementLayer>(s.pavement, context.surface);
}

LayerPtr BuildLayerSpec(const StrataLayerSpec& s, const std::string&, const LayerInputs& context) {
    return std::make_shared<StrataLayer>(s.strata, context.surface);
}

LayerPtr BuildLayerSpec(const SandShadowLayerSpec& s, const std::string&,
                        const LayerInputs& context) {
    return std::make_shared<SandShadowLayer>(context.boulders, s.shadows, context.surface);
}

LayerPtr BuildLayerSpec(const SandFillLayerSpec& s, const std::string&,
                        const LayerInputs& context) {
    return std::make_shared<SandFillLayer>(s.sandFill, context.surface);
}

LayerPtr BuildLayerSpec(const ScaleLayerSpec& s, const std::string&, const LayerInputs&) {
    return std::make_shared<ScaleLayer>(s.scale, s.offset);
}

LayerPtr BuildLayerSpec(const ClampLayerSpec& s, const std::string&, const LayerInputs&) {
    return std::make_shared<ClampLayer>(s.min, s.max);
}

LayerPtr BuildLayer(const LayerSpec& spec, const std::string& path, const LayerInputs& context) {
    return std::visit([&](const auto& layer) { return BuildLayerSpec(layer, path, context); },
                      spec);
}

/// The secondaries the secondary layers after the crater layer `layers[index]` throw from it, up
/// to the next crater layer; none when `layers[index]` is not a crater layer.
std::vector<SecondaryCraterOptions> ThrownSecondaries(const std::vector<LayerSpec>& layers,
                                                      std::size_t index) {
    std::vector<SecondaryCraterOptions> thrown;
    if (!std::holds_alternative<CraterLayerSpec>(layers[index])) {
        return thrown;
    }
    for (std::size_t j = index + 1;
         j < layers.size() && !std::holds_alternative<CraterLayerSpec>(layers[j]); ++j) {
        if (const auto* secondaries = std::get_if<SecondaryLayerSpec>(&layers[j])) {
            thrown.push_back(secondaries->secondaries);
        }
    }
    return thrown;
}

/// The generator of a checked configuration, built on `external` in place of its own source when
/// one is given: the layers then read `external` as their measured data.
TerrainGenerator BuildValidGenerator(const TerrainGenConfig& config,
                                     const std::filesystem::path& directory,
                                     const std::shared_ptr<const Source>& external = nullptr) {
    TerrainGenerator generator;
    generator.surface = BuildSurface(config.body, directory);
    if (external) {
        generator.source = external;
    } else {
        generator.source = config.source
                               ? BuildSource(*config.source, directory, generator.surface, "source")
                               : std::make_shared<SyntheticSource>();
    }
    const std::shared_ptr<const Source> given =
        external ? external : (config.source ? generator.source : nullptr);
    const std::shared_ptr<const Source> measured =
        external ? external
                 : (config.source ? MeasuredSource(*config.source, generator.source) : nullptr);
    std::vector<std::shared_ptr<const CraterLayer>> craters;
    std::shared_ptr<const BoulderLayer> boulders;
    for (std::size_t i = 0; i < config.layers.size(); ++i) {
        const LayerInputs context{
            ReferencePoint(generator), directory, craters,  boulders,
            generator.surface,         given,     measured, ThrownSecondaries(config.layers, i)};
        generator.layers.push_back(BuildLayer(config.layers[i], Element("layers", i), context));
        if (std::holds_alternative<CraterLayerSpec>(config.layers[i])) {
            craters.push_back(std::static_pointer_cast<const CraterLayer>(generator.layers.back()));
        }
        if (std::holds_alternative<BoulderLayerSpec>(config.layers[i])) {
            boulders = std::static_pointer_cast<const BoulderLayer>(generator.layers.back());
        }
    }
    return generator;
}

/// The bands of a raster albedo map, scaled as its metadata or the spec says.
std::vector<Raster<float>> LoadAlbedoBands(const RasterAlbedoSpec& spec,
                                           const std::filesystem::path& directory) {
    const std::filesystem::path path = Resolve(spec.path, directory);
    const GeoTiffInfo info = ReadGeoTiffInfo(path);
    std::vector<Raster<float>> bands;
    for (const std::size_t band : spec.bands) {
        Raster<float> raster = ReadGeoTiff(path, band).raster;
        const BandScaling scaling{spec.scale.value_or(info.bandScaling[band].scale),
                                  spec.offset.value_or(info.bandScaling[band].offset)};
        if (!scaling.Identity()) {
            for (float& value : raster.Values()) {
                if (!raster.IsNoData(value)) {
                    value = static_cast<float>(static_cast<double>(value) * scaling.scale +
                                               scaling.offset);
                }
            }
        }
        bands.push_back(std::move(raster));
    }
    return bands;
}

std::shared_ptr<const AlbedoSource> BuildAlbedoSource(const AlbedoSourceSpec& spec,
                                                      const TerrainGenerator& generator,
                                                      const std::filesystem::path& directory) {
    using Result = std::shared_ptr<const AlbedoSource>;
    return std::visit(Overloaded{
                          [&](const SyntheticAlbedoSpec& s) -> Result {
                              return std::make_shared<SyntheticAlbedo>(
                                  s.Options(), ReferencePoint(generator), generator.surface);
                          },
                          [&](const RasterAlbedoSpec& s) -> Result {
                              return std::make_shared<RasterAlbedo>(LoadAlbedoBands(s, directory),
                                                                    s.options, generator.surface);
                          },
                      },
                      spec);
}

/// What albedo layers are built from: the generator, and the configuration it was built from.
struct AlbedoInputs {
    const TerrainGenConfig& config;
    const TerrainGenerator& generator;
};

/// The generator's layer `index`, which the configuration built as a `T`.
/// @throws ConfigError if the generator has no `T` there, as when it was changed after building.
template <typename T>
std::shared_ptr<const T> GeneratorLayer(const AlbedoInputs& inputs, std::size_t index) {
    const std::vector<std::shared_ptr<const Layer>>& layers = inputs.generator.layers;
    std::shared_ptr<const T> layer =
        index < layers.size() ? std::dynamic_pointer_cast<const T>(layers[index]) : nullptr;
    if (!layer) {
        throw ConfigError(Element("layers", index),
                          "the generator's layer here is not the one the configuration builds; "
                          "pass the generator BuildGenerator built from this configuration");
    }
    return layer;
}

void AddAlbedoLayer(const CraterAlbedoSpec& craters, const AlbedoInputs& inputs, Albedo& albedo) {
    const std::vector<std::size_t> indices =
        config::LayerIndices<CraterLayerSpec>(inputs.config.layers);
    for (std::size_t i = 0; i < indices.size(); ++i) {
        if (!craters.craterLayer || *craters.craterLayer == i) {
            albedo.layers.push_back(std::make_shared<CraterAlbedo>(
                GeneratorLayer<CraterLayer>(inputs, indices[i]), craters.options));
        }
    }
}

void AddAlbedoLayer(const RockAlbedoSpec& rocks, const AlbedoInputs& inputs, Albedo& albedo) {
    std::vector<RockSource> sources;
    for (const std::size_t i : config::LayerIndices<BoulderLayerSpec>(inputs.config.layers)) {
        sources.push_back(
            {GeneratorLayer<BoulderLayer>(inputs, i), SharedGround(inputs.generator, i)});
    }
    if (rocks.boulderLayer) {
        sources = {sources[*rocks.boulderLayer]};
    }
    albedo.layers.push_back(std::make_shared<RockAlbedo>(rocks.options, std::move(sources)));
}

void AddAlbedoLayer(const SplotchAlbedoSpec& splotches, const AlbedoInputs& inputs,
                    Albedo& albedo) {
    albedo.layers.push_back(
        std::make_shared<SplotchAlbedo>(splotches.options, inputs.generator.surface));
}

void AddAlbedoLayer(const BlastZoneAlbedoSpec& zones, const AlbedoInputs& inputs, Albedo& albedo) {
    albedo.layers.push_back(
        std::make_shared<BlastZoneAlbedo>(zones.zones, inputs.generator.surface));
}

void AddAlbedoLayer(const SlopeAlbedoSpec& slopes, const AlbedoInputs& inputs, Albedo& albedo) {
    albedo.layers.push_back(
        std::make_shared<SlopeAlbedo>(slopes.options, inputs.generator.surface));
}

/// The radial shape a shape model spec describes, loaded once per file, modification time, Q and
/// unit, as validating and building a terrain both need it.
/// @throws ConfigError at "body.path" for a file that cannot be read or resampled.
std::shared_ptr<const RadialShape> LoadShapeModel(const ShapeModelSurfaceSpec& spec,
                                                  const std::filesystem::path& directory) {
    const std::filesystem::path file = Resolve(spec.path, directory);
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(file, error);
    if (error) {
        throw ConfigError("body.path", "cannot read " + file.string());
    }
    using Key = std::tuple<std::string, std::filesystem::file_time_type, std::size_t, double>;
    static std::mutex mutex;
    static std::map<Key, std::shared_ptr<const RadialShape>> loaded;
    const Key key{std::filesystem::absolute(file).string(), modified, spec.q.value_or(0),
                  spec.metersPerUnit.value_or(0.0)};
    const std::lock_guard<std::mutex> lock(mutex);
    if (const auto found = loaded.find(key); found != loaded.end()) {
        return found->second;
    }
    const auto shape = AtFile("body.path", [&] {
        if (IsIcqPath(file)) {
            const IcqModel model = ReadIcq(file, spec.metersPerUnit.value_or(1000.0));
            return spec.q && *spec.q != model.q ? ShapeModelFromMesh(IcqToMesh(model), *spec.q)
                                                : ToRadialShape(model);
        }
        const double scale = spec.metersPerUnit.value_or(1.0);
        const TriangleMesh mesh = Scaled(ReadMesh(file).mesh, {scale, scale, scale});
        return ShapeModelFromMesh(mesh, spec.q.value_or(256));
    });
    return loaded.emplace(key, std::make_shared<const RadialShape>(shape)).first->second;
}

}  // namespace

namespace config {

std::filesystem::path Resolve(const std::string& path, const std::filesystem::path& directory) {
    const std::filesystem::path resolved = path;
    return resolved.is_relative() && !directory.empty() ? directory / resolved : resolved;
}

BoulderLayerOptions BuildBoulderOptions(const BoulderLayerSpec& spec, const std::string& path,
                                        const std::filesystem::path& directory) {
    BoulderLayerOptions options = spec.boulders;
    if (options.sizes.distribution != DefaultBoulderSizes() ||
        options.ejecta.distribution != DefaultBoulderSizes()) {
        throw ConfigError(Child(path, "sizes"),
                          "gives the boulder sizes; leave boulders.sizes.distribution and "
                          "boulders.ejecta.distribution at their defaults and set sizes and "
                          "ejecta_sizes instead");
    }
    const auto sizes = [&](const BoulderSizesSpec& named, const std::string& key) {
        auto built = At(Child(path, key), [&] { return BuildBoulderSizes(named); });
        if (built && spec.extrapolate) {
            built->SetAllowExtrapolation(true);
        }
        return built;
    };
    options.sizes.distribution = sizes(spec.sizes, "sizes");
    options.ejecta.distribution = sizes(spec.ejectaSizes.value_or(spec.sizes), "ejecta_sizes");
    for (std::size_t i = 0; i < spec.shapeFiles.size(); ++i) {
        options.rendering.shapes.library.push_back(
            AtFile(Element(Child(path, "shape_files"), i), [&] {
                return std::make_shared<const SceneMesh>(
                    ReadMesh(Resolve(spec.shapeFiles[i], directory)));
            }));
    }
    if (spec.densityMap) {
        options.densityMap =
            BuildDensityMap(*spec.densityMap, Child(path, "density_map"), directory);
    }
    if (spec.slopeMap) {
        const SizeSlopeMapSpec& s = *spec.slopeMap;
        options.slopeMap =
            SizeSlopeMap{BuildField(s.field, Child(Child(path, "slope_map"), "field"), directory),
                         s.measure, s.referenceDiameter, s.minExponent, s.maxExponent};
    }
    options.manual = spec.manual;
    if (spec.completeness) {
        options.completeness =
            BuildField(*spec.completeness, Child(path, "completeness"), directory);
    }
    return options;
}

std::shared_ptr<const SurfaceField> BuildField(const FieldSpec& spec, const std::string& path,
                                               const std::filesystem::path& directory) {
    if (spec.path.empty()) {
        return std::make_shared<ConstantField>(spec.value);
    }
    RasterFieldOptions read;
    read.method = spec.method;
    read.scale = spec.scale;
    read.fallback = spec.fallback.value_or(std::numeric_limits<double>::quiet_NaN());
    read.periodicLongitude = spec.periodicLongitude;
    return AtFile(path,
                  [&] { return ReadRasterField(Resolve(spec.path, directory), read, spec.band); });
}

DensityMap BuildDensityMap(const DensityMapSpec& spec, const std::string& path,
                           const std::filesystem::path& directory) {
    DensityMap map;
    map.field = BuildField(spec.field, Child(path, "field"), directory);
    map.measure = spec.measure;
    map.minDiameter = spec.minDiameter;
    map.maxDiameter = spec.maxDiameter.value_or(std::numeric_limits<double>::infinity());
    map.areaUnit = spec.areaUnit;
    map.total = spec.total;
    return map;
}

std::optional<Bounds2d> ExtentBox(const ExtentSpec& extent) {
    if (extent.min.size() == 2 && extent.max.size() == 2) {
        return Bounds2d{extent.min[0], extent.min[1], extent.max[0], extent.max[1]};
    }
    if (extent.center.size() == 2 && extent.size.size() == 2) {
        const double halfX = 0.5 * extent.size[0];
        const double halfY = 0.5 * extent.size[1];
        return Bounds2d{extent.center[0] - halfX, extent.center[1] - halfY,
                        extent.center[0] + halfX, extent.center[1] + halfY};
    }
    return std::nullopt;
}

const OutputAreaSpec* OutputArea(const OutputSpec& output) {
    return std::visit(Overloaded{
                          [](const GeoTiffOutput& o) { return &o.area; },
                          [](const ListOutput& o) { return &o.area; },
                          [](const BoulderModelsOutput& o) { return &o.area; },
                          [](const TerrainMeshOutput& o) { return &o.area; },
                          [](const MeshOutput&) -> const OutputAreaSpec* { return nullptr; },
                          [](const ReportOutput&) -> const OutputAreaSpec* { return nullptr; },
                      },
                      output);
}

const std::optional<ExtentSpec>& SurfaceExtent(const OutputAreaSpec& area, const Surface& surface) {
    return surface.IsBody() ? area.extentDeg : area.extentM;
}

bool ListsFeatures(const OutputSpec& output) {
    return HoldsAny<CraterListOutput, BoulderListOutput, BoulderModelsOutput>(output);
}

RasterGrid WholeBodyGrid(const Surface& surface, double spacing) {
    const auto rows =
        static_cast<std::size_t>(std::max(1.0, std::ceil(180.0 * surface.UnitLength() / spacing)));
    return {2 * rows, rows, GeoTransform::NorthUp(-180.0, 90.0, 180.0 / static_cast<double>(rows))};
}

namespace {

/// Whether an output is a feature list over the whole body: a list on a body without an extent or
/// a source's grid, with a pixel size.
bool ListsWholeBody(const OutputSpec& output, const std::optional<RasterGrid>& source,
                    const Surface& surface) {
    const OutputAreaSpec* area = OutputArea(output);
    return ListsFeatures(output) && surface.IsBody() && !source && area->pixelSize &&
           !SurfaceExtent(*area, surface);
}

}  // namespace

std::optional<AreaCells> CoveredCells(const OutputAreaSpec& area,
                                      const std::optional<RasterGrid>& source,
                                      const Surface& surface) {
    const std::optional<ExtentSpec>& extent = SurfaceExtent(area, surface);
    const std::optional<Bounds2d> box =
        extent ? ExtentBox(*extent)
               : (source ? std::optional<Bounds2d>(source->Bounds()) : std::nullopt);
    const double cell = area.pixelSize ? *area.pixelSize / surface.UnitLength()
                                       : (source ? source->PixelSize() : 0.0);
    if (!box || !(cell > 0.0)) {
        return std::nullopt;
    }
    return AreaCells{*box, cell};
}

std::optional<RasterGrid> OutputGrid(const OutputSpec& output,
                                     const std::optional<RasterGrid>& source,
                                     const Surface& surface) {
    const OutputAreaSpec* area = OutputArea(output);
    if (!area) {
        return std::nullopt;
    }
    if (ListsWholeBody(output, source, surface)) {
        return WholeBodyGrid(surface, *area->pixelSize);
    }
    if (!SurfaceExtent(*area, surface) && !area->pixelSize) {
        return source;
    }
    const std::optional<AreaCells> cells = CoveredCells(*area, source, surface);
    return cells ? std::optional<RasterGrid>(RasterGrid::Covering(cells->box, cells->cell))
                 : std::nullopt;
}

double OutputSpacing(const OutputSpec& output, const std::optional<RasterGrid>& source,
                     const Surface& surface) {
    return ListsWholeBody(output, source, surface) ? *OutputArea(output)->pixelSize : 0.0;
}

SfdPtr BuildProduction(ProductionFunction production) {
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

Albedo BuildValidAlbedo(const TerrainGenConfig& config, const TerrainGenerator& generator,
                        const std::filesystem::path& directory) {
    Albedo albedo;
    albedo.surface = generator.surface;
    albedo.source = BuildAlbedoSource(config.albedo->source, generator, directory);
    const AlbedoInputs inputs{config, generator};
    for (const AlbedoLayerSpec& layer : config.albedo->layers) {
        std::visit([&](const auto& spec) { AddAlbedoLayer(spec, inputs, albedo); }, layer);
    }
    return albedo;
}

MaterialPhotometry BuildPhotometry(const PhotometrySpec& spec) {
    MaterialPhotometry photometry{};
    switch (spec.reference) {
    case PhotometryReference::MarsPancam:
        photometry = MarsPancamPhotometry();
        break;
    case PhotometryReference::None:
        break;
    }
    for (std::size_t k = 0; k < kMaterialClasses; ++k) {
        if (spec.classes[k]) {
            photometry[k] = spec.classes[k];
        }
    }
    return photometry;
}

RegionShape BuildRegionShape(const RegionShapeSpec& spec) {
    return std::visit(
        Overloaded{
            [](const RectangleRegionSpec& s) {
                return RegionShape::Rectangle({s.minX, s.minY, s.maxX, s.maxY});
            },
            [](const CircleRegionSpec& s) { return RegionShape::Circle({s.x, s.y}, s.radius); },
        },
        spec);
}

}  // namespace config

CraterModels BuildCraterModels(const CraterLayerSpec& craters, const std::string& path) {
    CraterModels models;
    models.density = At(Child(path, "density"), [&] { return BuildDensity(craters.density); });
    models.ages = At(Child(path, "ages"), [&] { return BuildAges(craters.ages); });
    models.profile = At(Child(path, "profile"),
                        [&] { return BuildProfile(craters.profile, craters.morphometry); });
    At(Child(path, "degradation"), [&] { AddDegradation(craters, models); });
    return models;
}

Surface BuildSurface(const SurfaceSpec& spec, const std::filesystem::path& directory) {
    SurfaceSpec ranged = spec;
    config::CheckRanges(ranged, "body", {}, {});
    return std::visit(
        Overloaded{
            [](const PlaneSurfaceSpec&) -> Surface { return Surface::Plane(); },
            [](const SphereSurfaceSpec& s) -> Surface {
                return At("body.radius", [&] { return Surface::Sphere(s.radius); });
            },
            [](const EllipsoidSurfaceSpec& s) -> Surface {
                if (s.radii.size() != 3) {
                    throw ConfigError("body.radii", "must hold exactly three semi-axes a, b, c");
                }
                return At("body.radii",
                          [&] { return Surface::Ellipsoid(s.radii[0], s.radii[1], s.radii[2]); });
            },
            [&](const ShapeModelSurfaceSpec& s) -> Surface {
                return Surface::ShapeModel(LoadShapeModel(s, directory));
            },
        },
        spec);
}

TerrainGenerator BuildGenerator(const TerrainGenConfig& config,
                                const std::filesystem::path& directory) {
    ValidateConfig(config, directory);
    return BuildValidGenerator(config, directory);
}

TerrainGenerator BuildGenerator(const TerrainGenConfig& config,
                                std::shared_ptr<const Source> source,
                                const std::filesystem::path& directory) {
    if (!source) {
        throw std::invalid_argument("BuildGenerator needs a source");
    }
    if (config.source) {
        throw ConfigError("source", "must be left out when the caller gives the source");
    }
    ValidateConfig(config, directory);
    return BuildValidGenerator(config, directory, source);
}

Albedo BuildAlbedo(const TerrainGenConfig& config, const TerrainGenerator& generator,
                   const std::filesystem::path& directory) {
    if (!config.albedo) {
        throw ConfigError("albedo", "the configuration has no albedo");
    }
    config::ValidateAlbedo(config, directory);
    return config::BuildValidAlbedo(config, generator, directory);
}

}  // namespace terraingen
