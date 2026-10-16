#include "terraingen/config/build.h"

#include "terraingen/albedo/crater_albedo.h"
#include "terraingen/albedo/raster_albedo.h"
#include "terraingen/albedo/synthetic.h"
#include "terraingen/config/fields/checks.h"
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
#include "terraingen/layers/craters/ages/steady_state.h"
#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/degradation/diffusivities.h"
#include "terraingen/layers/craters/degradation/freshness.h"
#include "terraingen/layers/craters/profiles/martin_parkes_dunstan.h"
#include "terraingen/layers/craters/shaping/morphometry.h"
#include "terraingen/layers/noise_relief.h"
#include "terraingen/layers/transforms.h"
#include "terraingen/mesh/mesh_io.h"
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
    return chronology == ChronologyModel::Ivanov ? Chronology(IvanovMarsChronology)
                                                 : Chronology(NeukumLunarChronology);
}

SfdPtr BuildEquilibrium(EquilibriumModel equilibrium) {
    return std::make_shared<PowerLawSfd>(
        equilibrium == EquilibriumModel::Hartmann ? HartmannEquilibrium() : TraskEquilibrium());
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
        },
        spec);
}

FreshMorphometry MorphometryOf(CraterMorphometryFit fit) {
    return fit == CraterMorphometryFit::Mars ? FreshMorphometry(MarsFreshCraterMorphometry)
                                             : FreshMorphometry(LunarFreshCraterMorphometry);
}

ComplexCraterShape ComplexShapeOf(CraterMorphometryFit fit) {
    return fit == CraterMorphometryFit::Mars ? MarsComplexCraterShape() : LunarComplexCraterShape();
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
                                  simple(s.simple), ComplexShapeOf(morphometry));
                          },
                      },
                      spec);
}

/// Sets `models.degradation` from `craters`; diffusion also wears `models.profile`.
void AddDegradation(const CraterLayerSpec& craters, CraterModels& models) {
    const FreshMorphometry fresh = MorphometryOf(craters.morphometry);
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
    const std::filesystem::path& directory;
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
        std::make_shared<NoDegradation>(MorphometryOf(spec.morphometry), spec.depthRatio)};
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
                              BoulderLayerOptions options =
                                  BuildBoulderOptions(s, path, context.directory);
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
    options.ejecta.distribution = sizes(spec.ejectaSizes, "ejecta_sizes");
    for (std::size_t i = 0; i < spec.shapeFiles.size(); ++i) {
        const std::string file = Element(Child(path, "shape_files"), i);
        try {
            options.rendering.shapes.library.push_back(std::make_shared<const SceneMesh>(
                ReadMesh(Resolve(spec.shapeFiles[i], directory))));
        } catch (const std::exception& error) {
            throw ConfigError(file, error.what());
        }
    }
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

Surface BuildSurface(const SurfaceSpec& spec) {
    return std::visit(Overloaded{
                          [](const PlaneSurfaceSpec&) -> Surface { return Surface::Plane(); },
                          [](const SphereSurfaceSpec& s) -> Surface {
                              if (!(std::isfinite(s.radius) && s.radius > 0.0)) {
                                  throw ConfigError("surface.radius",
                                                    "must be positive: the body's radius");
                              }
                              return Surface::Sphere(s.radius);
                          },
                      },
                      spec);
}

namespace {

Terrain BuildValidTerrain(const TerrainConfig& config, const std::filesystem::path& directory) {
    Terrain terrain;
    terrain.surface = BuildSurface(config.surface);
    terrain.source = BuildSource(config.source, directory, terrain.surface);
    std::shared_ptr<const CraterLayer> craters;
    for (std::size_t i = 0; i < config.layers.size(); ++i) {
        const LayerInputs context{*terrain.source, directory, craters, terrain.surface};
        terrain.layers.push_back(BuildLayer(config.layers[i], Element("layers", i), context));
        if (std::holds_alternative<CraterLayerSpec>(config.layers[i])) {
            craters = std::static_pointer_cast<const CraterLayer>(terrain.layers.back());
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

/// The top-level crater layers of a terrain built from `config`, in order.
std::vector<std::shared_ptr<const CraterLayer>> CraterLayersOf(const TerrainConfig& config,
                                                               const Terrain& terrain) {
    std::vector<std::shared_ptr<const CraterLayer>> craterLayers;
    for (std::size_t i = 0; i < config.layers.size(); ++i) {
        if (std::holds_alternative<CraterLayerSpec>(config.layers[i])) {
            craterLayers.push_back(std::static_pointer_cast<const CraterLayer>(terrain.layers[i]));
        }
    }
    return craterLayers;
}

Albedo BuildValidAlbedo(const TerrainConfig& config, const Terrain& terrain,
                        const std::filesystem::path& directory) {
    const AlbedoSpec& spec = *config.albedo;
    Albedo albedo;
    albedo.surface = terrain.surface;
    albedo.source = BuildAlbedoSource(spec.source, terrain, directory);
    const std::vector<std::shared_ptr<const CraterLayer>> craterLayers =
        CraterLayersOf(config, terrain);
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
}  // namespace

Terrain BuildTerrain(const TerrainConfig& config, const std::filesystem::path& directory) {
    ValidateConfig(config, directory);
    return BuildValidTerrain(config, directory);
}

Albedo BuildAlbedo(const TerrainConfig& config, const Terrain& terrain,
                   const std::filesystem::path& directory) {
    if (!config.albedo) {
        throw ConfigError("albedo", "the configuration has no albedo");
    }
    ValidateAlbedo(config, directory);
    return BuildValidAlbedo(config, terrain, directory);
}

}  // namespace terraingen
