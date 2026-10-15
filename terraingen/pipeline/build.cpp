/// @file
/// @brief Library objects built from configuration specs: models, layers, sources and albedo.
#include "terraingen/pipeline/detail/build.h"

#include "terraingen/albedo/crater_albedo.h"
#include "terraingen/albedo/raster_albedo.h"
#include "terraingen/albedo/synthetic.h"
#include "terraingen/bodies/mars/craters.h"
#include "terraingen/bodies/mars/production.h"
#include "terraingen/bodies/moon/craters.h"
#include "terraingen/bodies/moon/equilibrium.h"
#include "terraingen/bodies/moon/production.h"
#include "terraingen/bodies/moon/steady_state.h"
#include "terraingen/bodies/moon/surface.h"
#include "terraingen/distribution/log_polynomial.h"
#include "terraingen/distribution/power_law.h"
#include "terraingen/distribution/spliced.h"
#include "terraingen/distribution/tabulated.h"
#include "terraingen/layers/craters/ages/fresh.h"
#include "terraingen/layers/craters/ages/uniform.h"
#include "terraingen/layers/craters/degradation/freshness.h"
#include "terraingen/layers/craters/degradation/none.h"
#include "terraingen/layers/craters/profiles/martin_parkes_dunstan.h"
#include "terraingen/layers/noise_relief.h"
#include "terraingen/layers/transforms.h"
#include "terraingen/pipeline/config_checks.h"
#include "terraingen/pipeline/detail/validate.h"
#include "terraingen/pipeline/run.h"
#include "terraingen/sources/flat_source.h"
#include "terraingen/sources/geotiff_source.h"
#include "terraingen/sources/source_chain.h"
#include "terraingen/util/overloaded.h"

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace terraingen {

namespace {

using config::At;
using config::Child;
using config::Element;
using SfdPtr = std::shared_ptr<const SizeFrequencyDistribution>;
using LayerPtr = std::shared_ptr<const Layer>;

SfdPtr BuildDensity(const DensitySpec& spec) {
    return std::visit(
        Overloaded{
            [](const LunarSurfaceSpec& s) -> SfdPtr { return LunarSurface(s.ageGyr).density; },
            [](const MarsSurfaceSpec& s) -> SfdPtr { return MarsSurface(s.ageGyr).density; },
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

/// Neukum production with the Trask or Hartmann equilibrium.
std::shared_ptr<const CraterAgeModel> BuildSteadyStateAges(const SteadyStateAgesSpec& spec) {
    SfdPtr production;
    if (spec.extendedProduction) {
        production = std::make_shared<SplicedSfd>(ExtendedNeukumProductionFunction());
    } else {
        production = std::make_shared<LogPolynomialSfd>(NeukumProductionFunction());
    }
    const PowerLawSfd equilibrium =
        spec.equilibrium == EquilibriumModel::Trask ? TraskEquilibrium() : HartmannEquilibrium();
    return std::make_shared<SteadyStateAges>(production, std::make_shared<PowerLawSfd>(equilibrium),
                                             spec.ageGyr);
}

std::shared_ptr<const CraterAgeModel> BuildAges(const AgesSpec& spec, const DensitySpec& density) {
    using Result = std::shared_ptr<const CraterAgeModel>;
    return std::visit(
        Overloaded{
            [&](const AutoAgesSpec&) -> Result {
                if (const auto* lunar = std::get_if<LunarSurfaceSpec>(&density)) {
                    return LunarSurface(lunar->ageGyr).ages;
                }
                if (const auto* mars = std::get_if<MarsSurfaceSpec>(&density)) {
                    return MarsSurface(mars->ageGyr).ages;
                }
                return std::make_shared<UniformAges>(1.0);
            },
            [](const FreshAgesSpec&) -> Result { return std::make_shared<FreshAges>(); },
            [](const UniformAgesSpec& s) -> Result {
                return std::make_shared<UniformAges>(s.lifetimeGyr);
            },
            [](const SteadyStateAgesSpec& s) -> Result { return BuildSteadyStateAges(s); },
        },
        spec);
}

FreshMorphometry BodyMorphometry(CraterBody body) {
    return body == CraterBody::Mars ? FreshMorphometry(MarsFreshCraterMorphometry)
                                    : FreshMorphometry(LunarFreshCraterMorphometry);
}

DiffusionAgeModel BuildDiffusionAges(const DiffusionDegradationSpec& spec) {
    switch (spec.diffusivity) {
    case DiffusivityModel::Fassett2022Hartmann:
        return Fassett2022HartmannDiffusivity();
    case DiffusivityModel::Fassett2022Trask:
        return Fassett2022TraskDiffusivity();
    case DiffusivityModel::FassettThomson2014:
        return FassettThomson2014Diffusivity();
    case DiffusivityModel::FassettThomson2014Calibration:
        return [](double, double ageGyr) { return FassettThomson2014DiffusionAge(ageGyr); };
    case DiffusivityModel::PowerLaw:
        ValidatePowerLawDiffusivity(spec.powerLaw);
        return spec.powerLaw;
    }
    return Fassett2022HartmannDiffusivity();
}

std::shared_ptr<const CraterProfile> BuildProfile(const ProfileSpec& spec, CraterBody body) {
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
                                  simple(s.simple), body == CraterBody::Mars
                                                        ? MarsComplexCraterShape()
                                                        : LunarComplexCraterShape());
                          },
                      },
                      spec);
}

/// Sets `models.degradation` from `craters`; diffusion also wears `models.profile`.
void AddDegradation(const CraterLayerSpec& craters, CraterModels& models) {
    const FreshMorphometry fresh = BodyMorphometry(craters.body);
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
                const DiffusionAgeModel ages = BuildDiffusionAges(s);
                auto profile = std::make_shared<DiffusedProfile>(models.profile, s.reachRadii);
                models.degradation = std::make_shared<DiffusionDegradation>(fresh, ages, profile);
                models.profile = std::move(profile);
            },
        },
        craters.degradation);
}

/// @throws ConfigError for a density, ages or diffusivity model of another body than the layer's.
void CheckCraterBody(const CraterLayerSpec& craters, const std::string& path) {
    if (craters.body == CraterBody::Moon) {
        if (std::holds_alternative<MarsSurfaceSpec>(craters.density)) {
            throw ConfigError(Child(path, "density"),
                              "mars_surface is Mars's; set the layer's body to mars");
        }
        return;
    }
    if (std::holds_alternative<LunarSurfaceSpec>(craters.density)) {
        throw ConfigError(Child(path, "density"),
                          "lunar_surface is the Moon's; on Mars give mars_surface, a power_law, "
                          "piecewise_power_law or tabulated");
    }
    if (std::holds_alternative<SteadyStateAgesSpec>(craters.ages)) {
        throw ConfigError(Child(path, "ages"),
                          "steady_state ages are the Moon's; on Mars give fresh or uniform");
    }
    const auto* diffusion = std::get_if<DiffusionDegradationSpec>(&craters.degradation);
    if (diffusion && diffusion->diffusivity != DiffusivityModel::PowerLaw) {
        throw ConfigError(Child(Child(path, "degradation"), "diffusivity"),
                          "the diffusivity models are the Moon's; on Mars give a power_law");
    }
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
                return Sp8023IntercraterBlocks(s.curve);
            },
            [](const Sp8023CraterBouldersSpec& s) -> Result { return Sp8023CraterBlocks(s.curve); },
            [](const SynthterrainBouldersSpec& s) -> Result {
                return SynthterrainBoulderDistribution(s.model);
            },
            [&](const PowerLawSpec& s) -> Result { return density(s); },
            [&](const PiecewisePowerLawSpec& s) -> Result { return density(s); },
            [&](const TabulatedSpec& s) -> Result { return density(s); },
        },
        spec);
}

std::shared_ptr<const Source> BuildSource(const SourceSpec& spec,
                                          const std::filesystem::path& directory,
                                          const Surface& surface) {
    using Result = std::shared_ptr<const Source>;
    return std::visit(Overloaded{
                          [&](const FlatSourceSpec& s) -> Result {
                              return std::make_shared<FlatSource>(
                                  s.width, s.height, s.pixelSize / surface.UnitLength(),
                                  s.elevation, Vec2d{s.centreX, s.centreY});
                          },
                          [&](const RasterSourceSpec& s) -> Result {
                              std::optional<double> cell;
                              if (s.pixelSize) {
                                  cell = *s.pixelSize / surface.UnitLength();
                              }
                              return std::make_shared<GeoTiffSource>(Resolve(s.path, directory),
                                                                     s.band, cell, s.resampling,
                                                                     s.scale, s.offset);
                          },
                          [&](const ChainSourceSpec& s) -> Result {
                              std::vector<Result> sources;
                              for (const SourceSpec& source : s.sources) {
                                  sources.push_back(BuildSource(source, directory, surface));
                              }
                              return std::make_shared<SourceChain>(std::move(sources));
                          },
                      },
                      spec);
}

/// What a layer is built on: the source, the last top-level crater layer so far (for boulder
/// ejecta and secondaries) and the surface.
struct LayerInputs {
    const Source& source;
    const std::shared_ptr<const CraterLayer>& craters;
    const Surface& surface;
};

LayerPtr BuildCraterLayer(const CraterLayerSpec& craters, const std::string& path,
                          const Surface& surface) {
    CraterLayerOptions options;
    options.generate = craters.generate;
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
    return std::make_shared<CraterLayer>(BuildCraterModels(craters, path), std::move(options));
}

LayerPtr BuildSecondaryLayer(const SecondaryLayerSpec& spec,
                             const std::shared_ptr<const CraterLayer>& primaries) {
    const CraterShapeModels models{
        primaries->Models().profile,
        std::make_shared<NoDegradation>(BodyMorphometry(spec.body), spec.depthRatio)};
    return std::make_shared<SecondaryCraterLayer>(primaries, models, spec.secondaries);
}

LayerPtr BuildLayer(const LayerSpec& spec, const std::string& path, const LayerInputs& context) {
    const Surface& surface = context.surface;
    return std::visit(Overloaded{
                          [&](const NoiseLayerSpec& s) -> LayerPtr {
                              return std::make_shared<NoiseRelief>(
                                  s.Params(), context.source.ReferencePoint(), surface);
                          },
                          [&](const CraterLayerSpec& s) -> LayerPtr {
                              return BuildCraterLayer(s, path, surface);
                          },
                          [&](const BoulderLayerSpec& s) -> LayerPtr {
                              BoulderLayerOptions options = BuildBoulderOptions(s, path);
                              const bool throws = options.ejecta.MinCraterDiameter() > 0.0;
                              return std::make_shared<BoulderLayer>(
                                  std::move(options), throws ? context.craters : nullptr, surface);
                          },
                          [&](const RoughnessLayerSpec& s) -> LayerPtr {
                              return std::make_shared<RoughnessLayer>(s.roughness, surface);
                          },
                          [&](const BedformLayerSpec& s) -> LayerPtr {
                              return std::make_shared<BedformLayer>(s.bedforms, surface);
                          },
                          [&](const ScaleLayerSpec& s) -> LayerPtr {
                              return std::make_shared<ScaleLayer>(s.scale, s.offset);
                          },
                          [&](const ClampLayerSpec& s) -> LayerPtr {
                              return std::make_shared<ClampLayer>(s.min, s.max);
                          },
                          [&](const RegionLayerSpec& s) -> LayerPtr {
                              return std::make_shared<RegionLayer>(
                                  BuildLayer(*s.layer, Child(path, "layer"), context),
                                  BuildRegionShape(s.shape), s.feather, s.blend, surface);
                          },
                          [&](const SecondaryLayerSpec& s) -> LayerPtr {
                              return BuildSecondaryLayer(s, context.craters);
                          },
                      },
                      spec);
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
                                                      const Terrain& terrain,
                                                      const std::filesystem::path& directory) {
    using Result = std::shared_ptr<const AlbedoSource>;
    return std::visit(Overloaded{
                          [&](const SyntheticAlbedoSpec& s) -> Result {
                              return std::make_shared<SyntheticAlbedo>(
                                  s.Params(), terrain.source->ReferencePoint(), terrain.surface);
                          },
                          [&](const RasterAlbedoSpec& s) -> Result {
                              return std::make_shared<RasterAlbedo>(LoadAlbedoBands(s, directory),
                                                                    s.options, terrain.surface);
                          },
                      },
                      spec);
}

}  // namespace

std::filesystem::path Resolve(const std::string& path, const std::filesystem::path& directory) {
    const std::filesystem::path resolved = path;
    return resolved.is_relative() && !directory.empty() ? directory / resolved : resolved;
}

bool IsMeshFile(const std::filesystem::path& path) {
    const std::filesystem::path extension = path.extension();
    return extension == ".glb" || extension == ".ply";
}

CraterModels BuildCraterModels(const CraterLayerSpec& craters) {
    CraterModels models;
    models.density = BuildDensity(craters.density);
    models.ages = BuildAges(craters.ages, craters.density);
    models.profile = BuildProfile(craters.profile, craters.body);
    AddDegradation(craters, models);
    return models;
}

CraterModels BuildCraterModels(const CraterLayerSpec& craters, const std::string& path) {
    CraterModels models;
    models.density = At(Child(path, "density"), [&] { return BuildDensity(craters.density); });
    models.ages = At(Child(path, "ages"), [&] { return BuildAges(craters.ages, craters.density); });
    CheckCraterBody(craters, path);
    models.profile =
        At(Child(path, "profile"), [&] { return BuildProfile(craters.profile, craters.body); });
    At(Child(path, "degradation"), [&] { AddDegradation(craters, models); });
    return models;
}

BoulderLayerOptions BuildBoulderOptions(const BoulderLayerSpec& spec, const std::string& path) {
    BoulderLayerOptions options = spec.boulders;
    const auto sizes = [&](const BoulderSizesSpec& named, const std::string& key) {
        auto built = At(Child(path, key), [&] { return BuildBoulderSizes(named); });
        if (built && spec.extrapolate) {
            built->SetAllowExtrapolation(true);
        }
        return built;
    };
    options.sizes.distribution = sizes(spec.sizes, "sizes");
    options.ejecta.distribution = sizes(spec.ejectaSizes, "ejecta_sizes");
    return options;
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

Surface BuildSurface(const SurfaceSpec& spec) {
    return std::visit(
        Overloaded{
            [](const PlaneSurfaceSpec&) -> Surface { return Surface::Plane(); },
            [](const SphereSurfaceSpec& s) -> Surface {
                if (s.body) {
                    if (s.radius != 0.0) {
                        throw ConfigError("surface.radius", "give a radius or a body, not both");
                    }
                    return BodySphere(*s.body);
                }
                if (!(std::isfinite(s.radius) && s.radius > 0.0)) {
                    throw ConfigError("surface.radius",
                                      "must be positive; give the body's radius or a body");
                }
                return Surface::Sphere(s.radius);
            },
        },
        spec);
}

Terrain BuildValidTerrain(const TerrainConfig& config, const std::filesystem::path& directory) {
    Terrain terrain;
    terrain.surface = BuildSurface(config.surface);
    terrain.source = BuildSource(config.source, directory, terrain.surface);
    std::shared_ptr<const CraterLayer> craters;
    for (std::size_t i = 0; i < config.layers.size(); ++i) {
        const LayerInputs context{*terrain.source, craters, terrain.surface};
        terrain.layers.push_back(BuildLayer(config.layers[i], Element("layers", i), context));
        if (auto built = std::dynamic_pointer_cast<const CraterLayer>(terrain.layers.back())) {
            craters = std::move(built);
        }
    }
    for (const FilterSpec& filter : config.filters) {
        std::visit(
            [&](const RimSmoothing& smoothing) {
                terrain.filters.push_back(
                    std::make_shared<RimSmoothingFilter>(smoothing, terrain.surface));
            },
            filter);
    }
    return terrain;
}

Albedo BuildValidAlbedo(const AlbedoSpec& spec, const Terrain& terrain,
                        const std::filesystem::path& directory) {
    Albedo albedo;
    albedo.surface = terrain.surface;
    albedo.source = BuildAlbedoSource(spec.source, terrain, directory);
    std::vector<std::shared_ptr<const CraterLayer>> craterLayers;
    for (const auto& layer : terrain.layers) {
        if (auto craters = std::dynamic_pointer_cast<const CraterLayer>(layer)) {
            craterLayers.push_back(std::move(craters));
        }
    }
    for (const AlbedoLayerSpec& layer : spec.layers) {
        std::visit(
            [&](const CraterAlbedoSpec& craters) {
                for (std::size_t i = 0; i < craterLayers.size(); ++i) {
                    if (!craters.craterLayer || *craters.craterLayer == i) {
                        albedo.layers.push_back(
                            std::make_shared<CraterAlbedo>(craterLayers[i], craters.options));
                    }
                }
            },
            layer);
    }
    return albedo;
}

Albedo BuildAlbedo(const TerrainConfig& config, const Terrain& terrain,
                   const std::filesystem::path& directory) {
    if (!config.albedo) {
        throw ConfigError("albedo", "the configuration has no albedo");
    }
    ValidateAlbedo(config, directory, terrain.surface);
    return BuildValidAlbedo(*config.albedo, terrain, directory);
}

}  // namespace terraingen
