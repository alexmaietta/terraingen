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
#include "terraingen/layers/detail.h"
#include "terraingen/layers/noise_relief.h"
#include "terraingen/layers/transforms.h"
#include "terraingen/mesh/io/icq.h"
#include "terraingen/mesh/io/mesh_io.h"
#include "terraingen/mesh/shape_model.h"
#include "terraingen/sources/geotiff_source.h"
#include "terraingen/sources/source_chain.h"
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

DiffusionAgeModel BuildDiffusionAges(const DiffusionDegradationSpec& spec, const AgesSpec& ages) {
    switch (spec.diffusivity) {
    case DiffusivityModel::Matched: {
        const auto* steady = std::get_if<SteadyStateAgesSpec>(&ages);
        return steady && steady->equilibrium == EquilibriumModel::Trask
                   ? Fassett2022TraskDiffusivity()
                   : Fassett2022HartmannDiffusivity();
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
                const DiffusionAgeModel ages = BuildDiffusionAges(s, craters.ages);
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
            [&](const PiecewiseUniformSpec& s) -> Result { return density(s); },
        },
        spec);
}

std::shared_ptr<const Source> BuildSource(const SourceSpec& spec,
                                          const std::filesystem::path& directory,
                                          const Surface& surface) {
    using Result = std::shared_ptr<const Source>;
    return std::visit(Overloaded{
                          [&](const RasterSourceSpec& s) -> Result {
                              GeoTiffSourceOptions read = s.read;
                              if (read.pixelSize) {
                                  *read.pixelSize /= surface.UnitLength();
                              }
                              return std::make_shared<GeoTiffSource>(Resolve(s.path, directory),
                                                                     read);
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

/// The point noise is measured from: the base's reference point, or the origin without a base.
Vec2d ReferencePoint(const Terrain& terrain) {
    return terrain.source ? terrain.source->ReferencePoint() : Vec2d{};
}

/// What a layer is built on: the base's reference point, the last top-level crater and boulder
/// layers so far (for boulder ejecta, secondaries and sand shadows) and the surface.
struct LayerInputs {
    Vec2d referencePoint;
    const std::filesystem::path& directory;
    const std::shared_ptr<const CraterLayer>& craters;
    const std::shared_ptr<const BoulderLayer>& boulders;
    const Surface& surface;
    /// The secondaries later layers throw from a crater layer built with these inputs.
    std::vector<SecondaryCraterOptions> thrown{};
};

LayerPtr BuildCraterLayer(const CraterLayerSpec& craters, const std::string& path,
                          const Surface& surface, const std::filesystem::path& directory,
                          std::vector<SecondaryCraterOptions> thrown) {
    CraterLayerOptions options;
    options.secondaries = std::move(thrown);
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
    if (craters.densityMap) {
        options.densityMap =
            config::BuildDensityMap(*craters.densityMap, Child(path, "density_map"), directory);
    }
    if (craters.completeness) {
        options.generation.completeness =
            config::BuildField(*craters.completeness, Child(path, "completeness"), directory);
    }
    CraterModels models = BuildCraterModels(craters, path);
    return At(path,
              [&] { return std::make_shared<CraterLayer>(std::move(models), std::move(options)); });
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
    return std::visit(
        Overloaded{
            [&](const NoiseLayerSpec& s) -> LayerPtr {
                return std::make_shared<NoiseRelief>(s.Params(), context.referencePoint, surface);
            },
            [&](const CraterLayerSpec& s) -> LayerPtr {
                return BuildCraterLayer(s, path, surface, context.directory, context.thrown);
            },
            [&](const BoulderLayerSpec& s) -> LayerPtr {
                BoulderLayerOptions options = BuildBoulderOptions(s, path, context.directory);
                const bool throws = options.ejecta.MinCraterDiameter() > 0.0;
                return std::make_shared<BoulderLayer>(std::move(options),
                                                      throws ? context.craters : nullptr, surface);
            },
            [&](const RoughnessLayerSpec& s) -> LayerPtr {
                return std::make_shared<RoughnessLayer>(s.roughness, surface);
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
                return BuildSecondaryLayer(s, context.craters);
            },
            [&](const DemDetailLayerSpec& s) -> LayerPtr {
                const auto file = Resolve(s.path, context.directory);
                GeoTiffSourceOptions full = s.read;
                full.lowPass = 0.0;
                return std::make_shared<DetailLayer>(std::make_shared<GeoTiffSource>(file, full),
                                                     std::make_shared<GeoTiffSource>(file, s.read),
                                                     s.weight, surface);
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
                                  s.Params(), ReferencePoint(terrain), terrain.surface);
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
    if (spec.densityMap) {
        options.densityMap =
            BuildDensityMap(*spec.densityMap, Child(path, "density_map"), directory);
    }
    if (spec.slopeMap) {
        const SizeSlopeMapSpec& s = *spec.slopeMap;
        const std::string at = Child(path, "slope_map");
        options.slopeMap =
            SizeSlopeMap{BuildField(s.field, Child(at, "field"), directory), s.measure,
                         s.referenceDiameter, s.minExponent, s.maxExponent};
        At(at, [&] { ValidateSizeSlopeMap(*options.slopeMap); });
    }
    for (Crater boulder : spec.manual) {
        boulder.manual = true;
        options.manual.push_back(boulder);
    }
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
    try {
        return ReadRasterField(Resolve(spec.path, directory), read, spec.band);
    } catch (const std::exception& error) {
        throw ConfigError(path, error.what());
    }
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
    At(path, [&] { ValidateDensityMap(map); });
    return map;
}

RasterGrid RegionGrid(const GridRegionSpec& region, const Surface& surface) {
    const double cell = region.pixelSize / surface.UnitLength();
    const double halfX = 0.5 * static_cast<double>(region.width) * cell;
    const double halfY = 0.5 * static_cast<double>(region.height) * cell;
    return {region.width, region.height,
            GeoTransform::NorthUp(region.centreX - halfX, region.centreY + halfY, cell)};
}

RasterGrid WholeBodyGrid(const Surface& surface, double spacing) {
    const auto rows =
        static_cast<std::size_t>(std::max(1.0, std::ceil(180.0 * surface.UnitLength() / spacing)));
    return {2 * rows, rows, GeoTransform::NorthUp(-180.0, 90.0, 180.0 / static_cast<double>(rows))};
}

std::optional<RasterGrid> OutputGrid(const OutputSpec& output,
                                     const std::optional<RasterGrid>& base,
                                     const Surface& surface) {
    const auto covered = [&](const std::optional<GridRegionSpec>& region) {
        return region ? std::optional<RasterGrid>(RegionGrid(*region, surface)) : base;
    };
    const auto listed = [&](const auto& list) -> std::optional<RasterGrid> {
        if (list.region || base || !surface.IsBody() || !list.spacing) {
            return covered(list.region);
        }
        return WholeBodyGrid(surface, *list.spacing);
    };
    return std::visit(
        Overloaded{
            [&](const GeoTiffOutput& o) { return covered(o.region); },
            [&](const TerrainMeshOutput& o) { return covered(o.region); },
            [&](const ListOutput& o) { return listed(o); },
            [&](const BoulderModelsOutput& o) { return listed(o); },
            [](const MeshOutput&) -> std::optional<RasterGrid> { return std::nullopt; },
        },
        output);
}

MaterialPhotometry BuildPhotometry(const PhotometrySpec& spec) {
    MaterialPhotometry photometry = spec.reference == PhotometryReference::MarsPancam
                                        ? MarsPancamPhotometry()
                                        : MaterialPhotometry{};
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

namespace {

/// The radial shape a shape model spec describes, loaded once per file, modification time, Q and
/// unit, as validating and building a terrain both need it.
/// @throws ConfigError at "body.path", "body.q" or "body.metres_per_unit".
std::shared_ptr<const RadialShape> LoadShapeModel(const ShapeModelSurfaceSpec& spec,
                                                  const std::filesystem::path& directory) {
    config::RequireNonEmpty(spec.path, "body.path");
    if (spec.q && (*spec.q == 0 || *spec.q > 4096)) {
        throw ConfigError("body.q", "must be 1 to 4096");
    }
    if (spec.metresPerUnit && !(std::isfinite(*spec.metresPerUnit) && *spec.metresPerUnit > 0.0)) {
        throw ConfigError("body.metres_per_unit", "must be positive");
    }
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
    const auto shape = At("body.path", [&] {
        try {
            if (IsIcqPath(file)) {
                const IcqModel model = ReadIcq(file, spec.metresPerUnit.value_or(1000.0));
                return spec.q && *spec.q != model.q ? ShapeModelFromMesh(IcqToMesh(model), *spec.q)
                                                    : ToRadialShape(model);
            }
            const double scale = spec.metresPerUnit.value_or(1.0);
            const TriangleMesh mesh = Scaled(ReadMesh(file).mesh, {scale, scale, scale});
            return ShapeModelFromMesh(mesh, spec.q.value_or(256));
        } catch (const std::runtime_error& failure) {
            throw std::invalid_argument(failure.what());
        }
    });
    return loaded.emplace(key, std::make_shared<const RadialShape>(shape)).first->second;
}

}  // namespace

Surface BuildSurface(const SurfaceSpec& spec, const std::filesystem::path& directory) {
    return std::visit(
        Overloaded{
            [](const PlaneSurfaceSpec&) -> Surface { return Surface::Plane(); },
            [](const SphereSurfaceSpec& s) -> Surface {
                if (!(std::isfinite(s.radius) && s.radius > 0.0)) {
                    throw ConfigError("body.radius", "must be positive: the body's radius");
                }
                return Surface::Sphere(s.radius);
            },
            [](const EllipsoidSurfaceSpec& s) -> Surface {
                if (s.radii.size() != 3) {
                    throw ConfigError("body.radii", "must hold exactly three semi-axes a, b, c");
                }
                for (std::size_t i = 0; i < 3; ++i) {
                    if (!(std::isfinite(s.radii[i]) && s.radii[i] > 0.0)) {
                        throw ConfigError(Element("body.radii", i),
                                          "must be positive: a semi-axis in metres");
                    }
                }
                return Surface::Ellipsoid(s.radii[0], s.radii[1], s.radii[2]);
            },
            [&](const ShapeModelSurfaceSpec& s) -> Surface {
                return Surface::ShapeModel(LoadShapeModel(s, directory));
            },
        },
        spec);
}

namespace {

Terrain BuildValidTerrain(const TerrainConfig& config, const std::filesystem::path& directory) {
    Terrain terrain;
    terrain.surface = BuildSurface(config.body, directory);
    if (config.base) {
        terrain.source = BuildSource(*config.base, directory, terrain.surface);
    }
    std::shared_ptr<const CraterLayer> craters;
    std::shared_ptr<const BoulderLayer> boulders;
    for (std::size_t i = 0; i < config.layers.size(); ++i) {
        LayerInputs context{ReferencePoint(terrain), directory, craters, boulders, terrain.surface};
        for (std::size_t j = i + 1;
             std::holds_alternative<CraterLayerSpec>(config.layers[i]) &&
             j < config.layers.size() && !std::holds_alternative<CraterLayerSpec>(config.layers[j]);
             ++j) {
            if (const auto* secondaries = std::get_if<SecondaryLayerSpec>(&config.layers[j])) {
                context.thrown.push_back(secondaries->secondaries);
            }
        }
        terrain.layers.push_back(BuildLayer(config.layers[i], Element("layers", i), context));
        if (std::holds_alternative<CraterLayerSpec>(config.layers[i])) {
            craters = std::static_pointer_cast<const CraterLayer>(terrain.layers.back());
        }
        if (std::holds_alternative<BoulderLayerSpec>(config.layers[i])) {
            boulders = std::static_pointer_cast<const BoulderLayer>(terrain.layers.back());
        }
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
        std::visit(Overloaded{
                       [&](const CraterAlbedoSpec& craters) {
                           for (std::size_t i = 0; i < craterLayers.size(); ++i) {
                               if (!craters.craterLayer || *craters.craterLayer == i) {
                                   albedo.layers.push_back(std::make_shared<CraterAlbedo>(
                                       craterLayers[i], craters.options));
                               }
                           }
                       },
                       [&](const SplotchAlbedoSpec& splotches) {
                           albedo.layers.push_back(
                               std::make_shared<SplotchAlbedo>(splotches.options, terrain.surface));
                       },
                       [&](const BlastZoneAlbedoSpec& zones) {
                           albedo.layers.push_back(
                               std::make_shared<BlastZoneAlbedo>(zones.zones, terrain.surface));
                       },
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
