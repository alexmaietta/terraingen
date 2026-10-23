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
#include "terraingen/layers/craters/ages/freshness.h"
#include "terraingen/layers/craters/ages/steady_state.h"
#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/degradation/diffusivities.h"
#include "terraingen/layers/craters/degradation/freshness.h"
#include "terraingen/layers/craters/generation/resolved_limit.h"
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
using config::BuildRegionShape;
using config::Child;
using config::Element;
using config::Resolve;
using SfdPtr = std::shared_ptr<const SizeFrequencyDistribution>;
using LayerPtr = std::shared_ptr<const Layer>;

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
            [](const TraskSpec&) -> SfdPtr {
                return std::make_shared<PowerLawSfd>(TraskEquilibrium());
            },
            [](const HartmannSpec&) -> SfdPtr {
                return std::make_shared<PowerLawSfd>(HartmannEquilibrium());
            },
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
        return EquilibriumDiffusionAges(profile, fresh, [steady](double diameter) {
            return steady->EquilibriumLifetimeGyr(diameter);
        });
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

/// The point noise is measured from: the source's reference point, or the origin without one.
Vec2d ReferencePoint(const TerrainGenerator& generator) {
    return generator.source ? generator.source->ReferencePoint() : Vec2d{};
}

/// What a layer is built on: the base's reference point, the top-level crater layers so far (all
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
    /// The secondaries later layers throw from a crater layer built with these inputs.
    std::vector<SecondaryCraterOptions> thrown{};
};

/// The completeness of a crater layer's generation: its catalog's limit, the base's unresolved
/// limit, the minimum of the two when both are set, or null for neither. Without a base the
/// unresolved limit is infinite everywhere and so left out.
/// @param source The base; null without one.
std::shared_ptr<const SurfaceField> BuildCompleteness(const CraterLayerSpec& craters,
                                                      const std::string& path,
                                                      const std::filesystem::path& directory,
                                                      const std::shared_ptr<const Source>& source,
                                                      const Surface& surface) {
    std::shared_ptr<const SurfaceField> catalog;
    if (craters.completeness) {
        catalog = config::BuildField(*craters.completeness, Child(path, "completeness"), directory);
    }
    if (!craters.unresolvedPixelsAcross || !source) {
        return catalog;
    }
    const auto unresolved = At(Child(path, "unresolved_pixels_across"), [&] {
        return UnresolvedCraterLimit(source, *craters.unresolvedPixelsAcross, surface);
    });
    return catalog ? std::make_shared<const MinimumField>(catalog, unresolved) : unresolved;
}

LayerPtr BuildCraterLayer(const CraterLayerSpec& craters, const std::string& path,
                          const Surface& surface, const std::filesystem::path& directory,
                          const std::shared_ptr<const Source>& source,
                          std::vector<SecondaryCraterOptions> thrown) {
    CraterLayerOptions options;
    options.secondaries = std::move(thrown);
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
    options.surface = surface;
    if (craters.densityMap) {
        options.densityMap =
            config::BuildDensityMap(*craters.densityMap, Child(path, "density_map"), directory);
    }
    options.generation.completeness = BuildCompleteness(craters, path, directory, source, surface);
    CraterModels models = BuildCraterModels(craters, path);
    return At(path,
              [&] { return std::make_shared<CraterLayer>(std::move(models), std::move(options)); });
}

/// The options of a spectral continuation layer, fitted to the base wherever the spec gives no
/// slope or amplitude; a valid configuration has a base whenever the spec leaves one out.
SpectralContinuationOptions BuildSpectralOptions(const SpectralContinuationLayerSpec& spec,
                                                 const Source* source, const Surface& surface,
                                                 const std::string& path) {
    SpectralContinuationOptions options;
    options.minWavelength = spec.minWavelength;
    options.taperOctaves = spec.taperOctaves;
    options.seed = spec.seed;
    options.fade = spec.fade;
    const RasterGrid grid = source ? source->Grid() : RasterGrid{};
    options.pixelSize = spec.pixelSize.value_or(grid.PixelSize() * surface.UnitLength());
    if (spec.slope && spec.octaveRms) {
        options.slope = *spec.slope;
        options.octaveRms = *spec.octaveRms;
        return options;
    }
    const PowerLawSpectrum fit = At(path, [&] {
        const Raster<float> heights =
            SampleSourceWindow(*source, options.pixelSize / surface.UnitLength(),
                               grid.transform.PixelToWorld(0.5, 0.5));
        return EstimatePowerLawSpectrum(heights, spec.fit);
    });
    options.slope = spec.slope.value_or(fit.slope);
    options.octaveRms = spec.octaveRms.value_or(fit.octaveRms);
    return options;
}

/// The roughness of a layer's base at its native resolution (`EstimateSourceRoughness`).
/// @throws std::invalid_argument without a base, or as `EstimateSourceRoughness`.
BaseRoughness BaseRoughnessAt(const LayerInputs& context, const Surface& surface) {
    if (!context.source) {
        throw std::invalid_argument("Estimating from the base needs a base");
    }
    return EstimateSourceRoughness(*context.source, surface);
}

LayerPtr BuildSecondaryLayer(const SecondaryLayerSpec& spec,
                             const std::shared_ptr<const CraterLayer>& primaries) {
    const CraterShapeModels models{
        primaries->Models().profile,
        std::make_shared<NoDegradation>(BuildMorphometry(spec.morphometry), spec.depthRatio)};
    return std::make_shared<SecondaryCraterLayer>(primaries, models, spec.secondaries);
}

LayerPtr BuildLayer(const LayerSpec& spec, const std::string& path, const LayerInputs& context) {
    const Surface& surface = context.surface;
    return std::visit(
        Overloaded{
            [&](const NoiseLayerSpec& s) -> LayerPtr {
                const SyntheticTerrainParams params = At(path, [&] {
                    return s.estimateFromBase ? s.EstimatedParams(BaseRoughnessAt(context, surface))
                                              : s.Params();
                });
                return std::make_shared<NoiseRelief>(params, context.referencePoint, surface);
            },
            [&](const CraterLayerSpec& s) -> LayerPtr {
                return BuildCraterLayer(s, path, surface, context.directory, context.source,
                                        context.thrown);
            },
            [&](const BoulderLayerSpec& s) -> LayerPtr {
                BoulderLayerOptions options = BuildBoulderOptions(s, path, context.directory);
                const bool throws =
                    options.generateRandom && options.ejecta.MinCraterDiameter() > 0.0;
                return std::make_shared<BoulderLayer>(
                    std::move(options),
                    throws ? context.craters : std::vector<std::shared_ptr<const CraterLayer>>{},
                    surface);
            },
            [&](const RoughnessLayerSpec& s) -> LayerPtr {
                const RoughnessOptions options = At(path, [&] {
                    return s.estimateFromBase
                               ? s.EstimatedOptions(BaseRoughnessAt(context, surface))
                               : s.Options();
                });
                return std::make_shared<RoughnessLayer>(options, surface);
            },
            [&](const DuneLayerSpec& s) -> LayerPtr {
                return std::make_shared<DuneLayer>(s.dunes, surface);
            },
            [&](const PavementLayerSpec& s) -> LayerPtr {
                return std::make_shared<PavementLayer>(s.pavement, surface);
            },
            [&](const StrataLayerSpec& s) -> LayerPtr {
                return std::make_shared<StrataLayer>(s.strata, surface);
            },
            [&](const SandShadowLayerSpec& s) -> LayerPtr {
                return std::make_shared<SandShadowLayer>(context.boulders, s.shadows, surface);
            },
            [&](const SandFillLayerSpec& s) -> LayerPtr {
                return std::make_shared<SandFillLayer>(s.sandFill, surface);
            },
            [&](const BedformLayerSpec& s) -> LayerPtr {
                return std::make_shared<BedformLayer>(s.bedforms, surface);
            },
            [&](const SpectralContinuationLayerSpec& s) -> LayerPtr {
                return std::make_shared<SpectralContinuationLayer>(
                    BuildSpectralOptions(s, context.source.get(), surface, path), surface);
            },
            [&](const ScaleLayerSpec& s) -> LayerPtr {
                return std::make_shared<ScaleLayer>(s.scale, s.offset);
            },
            [&](const ClampLayerSpec& s) -> LayerPtr {
                return std::make_shared<ClampLayer>(s.min, s.max);
            },
            [&](const RegionLayerSpec& s) -> LayerPtr {
                return std::make_shared<RegionLayer>(
                    BuildLayer(*s.layer, Child(path, "layer"), context), BuildRegionShape(s.shape),
                    s.feather, s.blend, surface);
            },
            [&](const SecondaryLayerSpec& s) -> LayerPtr {
                return BuildSecondaryLayer(s, context.craters.back());
            },
        },
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

TerrainGenerator BuildValidGenerator(const TerrainGenConfig& config,
                                     const std::filesystem::path& directory) {
    TerrainGenerator generator;
    generator.surface = BuildSurface(config.body, directory);
    generator.source = config.source
                           ? BuildSource(*config.source, directory, generator.surface, "source")
                           : std::make_shared<SyntheticSource>();
    const std::shared_ptr<const Source> given = config.source ? generator.source : nullptr;
    std::vector<std::shared_ptr<const CraterLayer>> craters;
    std::shared_ptr<const BoulderLayer> boulders;
    for (std::size_t i = 0; i < config.layers.size(); ++i) {
        const LayerInputs context{ReferencePoint(generator),
                                  directory,
                                  craters,
                                  boulders,
                                  generator.surface,
                                  given,
                                  ThrownSecondaries(config.layers, i)};
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
                                  s.Params(), ReferencePoint(generator), generator.surface);
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

void AddAlbedoLayer(const CraterAlbedoSpec& craters, const AlbedoInputs& inputs, Albedo& albedo) {
    const std::vector<std::size_t> indices =
        config::LayerIndices<CraterLayerSpec>(inputs.config.layers);
    for (std::size_t i = 0; i < indices.size(); ++i) {
        if (!craters.craterLayer || *craters.craterLayer == i) {
            albedo.layers.push_back(std::make_shared<CraterAlbedo>(
                std::static_pointer_cast<const CraterLayer>(inputs.generator.layers[indices[i]]),
                craters.options));
        }
    }
}

void AddAlbedoLayer(const RockAlbedoSpec& rocks, const AlbedoInputs& inputs, Albedo& albedo) {
    std::vector<RockSource> sources;
    for (const std::size_t i : config::LayerIndices<BoulderLayerSpec>(inputs.config.layers)) {
        sources.push_back({std::static_pointer_cast<const BoulderLayer>(inputs.generator.layers[i]),
                           SharedGround(inputs.generator, i)});
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
                  spec.metresPerUnit.value_or(0.0)};
    const std::lock_guard<std::mutex> lock(mutex);
    if (const auto found = loaded.find(key); found != loaded.end()) {
        return found->second;
    }
    const auto shape = AtFile("body.path", [&] {
        if (IsIcqPath(file)) {
            const IcqModel model = ReadIcq(file, spec.metresPerUnit.value_or(1000.0));
            return spec.q && *spec.q != model.q ? ShapeModelFromMesh(IcqToMesh(model), *spec.q)
                                                : ToRadialShape(model);
        }
        const double scale = spec.metresPerUnit.value_or(1.0);
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

double SnappedCells(double span, double cell) {
    return std::max(1.0, std::ceil(span / cell - kExtentSnapTolerance));
}

RasterGrid SnappedGrid(const Bounds2d& box, double cell) {
    const double columns = SnappedCells(box.maxX - box.minX, cell);
    const double rows = SnappedCells(box.maxY - box.minY, cell);
    return {static_cast<std::size_t>(columns), static_cast<std::size_t>(rows),
            GeoTransform::NorthUp(box.minX, box.minY + rows * cell, cell)};
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
bool ListsWholeBody(const OutputSpec& output, const std::optional<RasterGrid>& base,
                    const Surface& surface) {
    const OutputAreaSpec* area = OutputArea(output);
    return ListsFeatures(output) && surface.IsBody() && !base && area->pixelSize &&
           !SurfaceExtent(*area, surface);
}

}  // namespace

std::optional<RasterGrid> OutputGrid(const OutputSpec& output,
                                     const std::optional<RasterGrid>& base,
                                     const Surface& surface) {
    const OutputAreaSpec* area = OutputArea(output);
    if (!area) {
        return std::nullopt;
    }
    if (ListsWholeBody(output, base, surface)) {
        return WholeBodyGrid(surface, *area->pixelSize);
    }
    const std::optional<ExtentSpec>& extent = SurfaceExtent(*area, surface);
    if (!extent && !area->pixelSize) {
        return base;
    }
    const std::optional<Bounds2d> box =
        extent ? ExtentBox(*extent)
               : (base ? std::optional<Bounds2d>(base->Bounds()) : std::nullopt);
    const double cell = area->pixelSize ? *area->pixelSize / surface.UnitLength()
                                        : (base ? base->PixelSize() : 0.0);
    if (!box || !(cell > 0.0)) {
        return std::nullopt;
    }
    return SnappedGrid(*box, cell);
}

double OutputSpacing(const OutputSpec& output, const std::optional<RasterGrid>& base,
                     const Surface& surface) {
    return ListsWholeBody(output, base, surface) ? *OutputArea(output)->pixelSize : 0.0;
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

Albedo BuildAlbedo(const TerrainGenConfig& config, const TerrainGenerator& generator,
                   const std::filesystem::path& directory) {
    if (!config.albedo) {
        throw ConfigError("albedo", "the configuration has no albedo");
    }
    ValidateAlbedo(config, directory);
    return BuildValidAlbedo(config, generator, directory);
}

}  // namespace terraingen
