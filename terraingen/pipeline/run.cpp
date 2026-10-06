#include "terraingen/pipeline/run.h"

#include "terraingen/bodies/moon/equilibrium.h"
#include "terraingen/bodies/moon/production.h"
#include "terraingen/bodies/moon/steady_state.h"
#include "terraingen/bodies/moon/surface.h"
#include "terraingen/distribution/golombek_rapp.h"
#include "terraingen/distribution/log_polynomial.h"
#include "terraingen/distribution/power_law.h"
#include "terraingen/distribution/spliced.h"
#include "terraingen/distribution/tabulated.h"
#include "terraingen/layers/craters/ages/fresh.h"
#include "terraingen/layers/craters/ages/uniform.h"
#include "terraingen/layers/craters/degradation/freshness.h"
#include "terraingen/layers/craters/degradation/none.h"
#include "terraingen/layers/craters/filters.h"
#include "terraingen/layers/craters/profiles/martin_parkes_dunstan.h"
#include "terraingen/layers/noise_relief.h"
#include "terraingen/layers/transforms.h"
#include "terraingen/noise/kernels/layers.h"
#include "terraingen/pipeline/config_json.h"
#include "terraingen/sources/flat_source.h"
#include "terraingen/sources/geotiff_source.h"
#include "terraingen/sources/source_chain.h"
#include "terraingen/util/log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <variant>

namespace terraingen {

namespace {

template <typename... Fs> struct Overloaded : Fs... {
    using Fs::operator()...;
};

std::shared_ptr<const SizeFrequencyDistribution> BuildDensity(const DensitySpec& spec) {
    return std::visit(
        Overloaded{
            [](const LunarSurfaceSpec& s) -> std::shared_ptr<const SizeFrequencyDistribution> {
                return LunarSurface(s.ageGyr).density;
            },
            [](const PowerLawSpec& s) -> std::shared_ptr<const SizeFrequencyDistribution> {
                return std::make_shared<PowerLawSfd>(s.coefficient, s.exponent);
            },
            [](const PiecewisePowerLawSpec& s) -> std::shared_ptr<const SizeFrequencyDistribution> {
                const double start = s.segments.empty() ? 0.0 : s.segments.front().start;
                return std::make_shared<PiecewisePowerLawSfd>(s.segments, SfdKind::Density,
                                                              DiameterRange{start});
            },
            [](const TabulatedSpec& s) -> std::shared_ptr<const SizeFrequencyDistribution> {
                return std::make_shared<TabulatedSfd>(s.diameters, s.cumulative, SfdKind::Density,
                                                      s.diameterUnit, s.areaUnit);
            },
            [](const TraskSpec&) -> std::shared_ptr<const SizeFrequencyDistribution> {
                return std::make_shared<PowerLawSfd>(TraskEquilibrium());
            },
            [](const HartmannSpec&) -> std::shared_ptr<const SizeFrequencyDistribution> {
                return std::make_shared<PowerLawSfd>(HartmannEquilibrium());
            },
            [](const MooreSpec& s) -> std::shared_ptr<const SizeFrequencyDistribution> {
                return std::make_shared<PiecewisePowerLawSfd>(MooreDistribution(s.terrain));
            },
            [](const ViperSpec&) -> std::shared_ptr<const SizeFrequencyDistribution> {
                return std::make_shared<PiecewisePowerLawSfd>(ViperSpecification());
            },
        },
        spec);
}

std::shared_ptr<const CraterAgeModel> BuildAges(const AgesSpec& spec, const DensitySpec& density) {
    return std::visit(
        Overloaded{
            [&](const AutoAgesSpec&) -> std::shared_ptr<const CraterAgeModel> {
                if (const auto* lunar = std::get_if<LunarSurfaceSpec>(&density)) {
                    return LunarSurface(lunar->ageGyr).ages;
                }
                return std::make_shared<UniformAges>(1.0);
            },
            [](const FreshAgesSpec&) -> std::shared_ptr<const CraterAgeModel> {
                return std::make_shared<FreshAges>();
            },
            [](const UniformAgesSpec& s) -> std::shared_ptr<const CraterAgeModel> {
                return std::make_shared<UniformAges>(s.lifetimeGyr);
            },
            [](const SteadyStateAgesSpec& s) -> std::shared_ptr<const CraterAgeModel> {
                std::shared_ptr<const SizeFrequencyDistribution> production;
                if (s.extendedProduction) {
                    production = std::make_shared<SplicedSfd>(ExtendedNeukumProductionFunction());
                } else {
                    production = std::make_shared<LogPolynomialSfd>(NeukumProductionFunction());
                }
                const PowerLawSfd equilibrium = s.equilibrium == EquilibriumModel::Trask
                                                    ? TraskEquilibrium()
                                                    : HartmannEquilibrium();
                return std::make_shared<SteadyStateAges>(
                    production, std::make_shared<PowerLawSfd>(equilibrium), s.ageGyr);
            },
        },
        spec);
}

std::shared_ptr<const DegradationModel>
BuildDegradation(const DegradationSpec& spec, const std::shared_ptr<const CraterAgeModel>& ages) {
    return std::visit(
        Overloaded{
            [&](const FreshnessDegradationSpec& s) -> std::shared_ptr<const DegradationModel> {
                return std::make_shared<FreshnessDegradation>(ages, s.cutoffDepthRatio);
            },
            [](const NoDegradationSpec& s) -> std::shared_ptr<const DegradationModel> {
                return s.depthRatio ? std::make_shared<NoDegradation>(*s.depthRatio)
                                    : std::make_shared<NoDegradation>();
            },
        },
        spec);
}

std::shared_ptr<const CraterProfile> BuildProfile(const ProfileSpec& spec) {
    return std::visit(
        Overloaded{
            [](const FassettThomsonSpec& s) -> std::shared_ptr<const CraterProfile> {
                return std::make_shared<FassettThomsonProfile>(s.rim);
            },
            [](const MartinParkesDunstanSpec&) -> std::shared_ptr<const CraterProfile> {
                return std::make_shared<MartinParkesDunstanProfile>();
            },
        },
        spec);
}

/// Runs `check`, rethrowing any invalid-argument or domain error as a `ConfigError` at `path`.
template <typename Check> auto At(const std::string& path, Check check) {
    try {
        return check();
    } catch (const std::invalid_argument& error) {
        throw ConfigError(path, error.what());
    } catch (const std::domain_error& error) {
        throw ConfigError(path, error.what());
    }
}

std::filesystem::path Resolve(const std::string& path, const std::filesystem::path& directory) {
    const std::filesystem::path resolved = path;
    return resolved.is_relative() && !directory.empty() ? directory / resolved : resolved;
}

void ValidateLayers(const std::vector<NoiseLayerConfig>& layers, const std::string& path,
                    bool allowEmpty) {
    if ((layers.empty() && !allowEmpty) || layers.size() > kMaxNoiseLayers) {
        throw ConfigError(path, std::string("needs ") + (allowEmpty ? "0" : "1") + " to " +
                                    std::to_string(kMaxNoiseLayers) + " layers");
    }
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const std::string layer = path + "[" + std::to_string(i) + "]";
        if (layers[i].hurst && !std::isfinite(*layers[i].hurst)) {
            throw ConfigError(layer + ".hurst", "must be finite");
        }
        At(layer, [&] { ValidateNoiseLayer(layers[i].Effective()); });
    }
}

void CheckGridSize(std::size_t width, std::size_t height, const std::string& path) {
    if (width < 2 || height < 2 || width > kMaxConfigGridSize || height > kMaxConfigGridSize) {
        throw ConfigError(path, std::to_string(width) + "x" + std::to_string(height) +
                                    " cells; each side must be between 2 and " +
                                    std::to_string(kMaxConfigGridSize));
    }
}

SyntheticTerrainParams StackParams(const std::vector<NoiseLayerConfig>& stack) {
    SyntheticTerrainParams params;
    params.layers.clear();
    for (const NoiseLayerConfig& layer : stack) {
        params.layers.push_back(layer.Effective());
    }
    return params;
}

void ValidateCraters(const CraterLayerSpec& craters, const RasterGrid& grid,
                     const std::string& path) {
    for (std::size_t n = 0; n < craters.manual.size(); ++n) {
        const Crater& crater = craters.manual[n];
        const std::string manual = path + ".manual[" + std::to_string(n) + "]";
        if (!(std::isfinite(crater.x) && std::isfinite(crater.y))) {
            throw ConfigError(manual, "position must be finite");
        }
        if (!(std::isfinite(crater.diameter) && crater.diameter > 0.0)) {
            throw ConfigError(manual + ".diameter", "must be positive");
        }
        if (!(std::isfinite(crater.ageGyr) && crater.ageGyr >= 0.0)) {
            throw ConfigError(manual + ".age_gyr", "must not be negative");
        }
    }
    At(path + ".spacing", [&] { ValidateCraterSpacing(craters.spacing); });
    At(path + ".apply", [&] { ValidateCraterApplyOptions(craters.apply); });
    const auto density = At(path + ".density", [&] { return BuildDensity(craters.density); });
    const auto ages = At(path + ".ages", [&] { return BuildAges(craters.ages, craters.density); });
    At(path + ".degradation", [&] { return BuildDegradation(craters.degradation, ages); });
    At(path + ".profile", [&] { return BuildProfile(craters.profile); });
    if (!craters.generate) {
        return;
    }
    if (!(std::isfinite(craters.minDiameter) && craters.minDiameter > 0.0)) {
        throw ConfigError(path + ".min_diameter", "must be positive");
    }
    if (!(std::isfinite(craters.maxDiameter) && craters.maxDiameter > craters.minDiameter)) {
        throw ConfigError(path + ".max_diameter", "must exceed min_diameter");
    }
    const double area = grid.Area();
    const double expected = At(path + ".min_diameter", [&] {
        return density->ExpectedCount(area, craters.minDiameter, craters.maxDiameter);
    });
    const double limit = CraterGenerationOptions{}.maxExpectedCraters;
    if (expected > limit) {
        throw ConfigError(path + ".min_diameter",
                          "about " + std::to_string(static_cast<long long>(expected)) +
                              " craters expected, more than the limit of " +
                              std::to_string(static_cast<long long>(limit)) +
                              "; raise min_diameter or set max_craters");
    }
}

void ValidateRocks(const RockLayerSpec& spec, bool craters, const RasterGrid& grid,
                   const std::string& path) {
    const RockLayerOptions& rocks = spec.rocks;
    At(path, [&] { ValidateRockLayerOptions(rocks); });
    if (rocks.ejecta.minCraterDiameter > 0.0 && !craters) {
        throw ConfigError(path + ".ejecta.min_crater_diameter",
                          "ejecta need a crater layer before this one");
    }
    const double minDiameter =
        std::fmax(rocks.minDiameter, rocks.fade.startSamples * grid.PixelSize());
    if (minDiameter >= rocks.maxDiameter) {
        return;
    }
    const GolombekRappSfd candidates(RockCandidateCoverage(rocks), rocks.qa, rocks.qb);
    const double expected = candidates.ExpectedCount(grid.Area(), minDiameter, rocks.maxDiameter);
    const double limit = CraterGenerationOptions{}.maxExpectedCraters;
    if (expected > limit) {
        throw ConfigError(path + ".min_diameter",
                          "about " + std::to_string(static_cast<long long>(expected)) +
                              " candidate rocks expected, more than the limit of " +
                              std::to_string(static_cast<long long>(limit)) +
                              "; raise min_diameter or lower coverage");
    }
}

RegionShape BuildRegionShape(const RegionShapeSpec& spec) {
    if (const auto* circle = std::get_if<CircleRegionSpec>(&spec)) {
        return RegionShape::Circle({circle->x, circle->y}, circle->radius);
    }
    const auto& rectangle = std::get<RectangleRegionSpec>(spec);
    return RegionShape::Rectangle({rectangle.minX, rectangle.minY, rectangle.maxX, rectangle.maxY});
}

/// Checks the layer `spec` at JSON `path`; `craters` says whether a crater layer came before it
/// and is set when `spec` is one.
void ValidateLayer(const LayerSpec& spec, const RasterGrid& grid, const std::string& path,
                   bool& craters) {
    std::visit(Overloaded{
                   [&](const NoiseLayerSpec& noise) {
                       ValidateLayers(noise.stack, path + ".stack", false);
                   },
                   [&](const CraterLayerSpec& layer) {
                       ValidateCraters(layer, grid, path);
                       craters = true;
                   },
                   [&](const RockLayerSpec& layer) { ValidateRocks(layer, craters, grid, path); },
                   [&](const RoughnessLayerSpec& layer) {
                       At(path, [&] { ValidateRoughnessOptions(layer.roughness); });
                   },
                   [&](const ScaleLayerSpec& layer) {
                       At(path, [&] { ScaleLayer(layer.scale, layer.offset); });
                   },
                   [&](const ClampLayerSpec& layer) {
                       At(path, [&] { ClampLayer(layer.min, layer.max); });
                   },
                   [&](const RegionLayerSpec& layer) {
                       At(path + ".shape",
                          [&] { ValidateRegionShape(BuildRegionShape(layer.shape)); });
                       if (!(std::isfinite(layer.feather) && layer.feather >= 0.0)) {
                           throw ConfigError(path + ".feather", "must not be negative");
                       }
                       bool inner = craters;
                       ValidateLayer(*layer.layer, grid, path + ".layer", inner);
                   },
               },
               spec);
}

/// The layer `spec` describes; rock ejecta come from `ejecta`, the last crater layer before it.
std::shared_ptr<const Layer> BuildLayer(const LayerSpec& spec, const Source& source,
                                        const std::shared_ptr<const CraterLayer>& ejecta) {
    if (const auto* noise = std::get_if<NoiseLayerSpec>(&spec)) {
        return std::make_shared<NoiseRelief>(noise->Params(), source.ReferencePoint());
    }
    if (const auto* scale = std::get_if<ScaleLayerSpec>(&spec)) {
        return std::make_shared<ScaleLayer>(scale->scale, scale->offset);
    }
    if (const auto* clamp = std::get_if<ClampLayerSpec>(&spec)) {
        return std::make_shared<ClampLayer>(clamp->min, clamp->max);
    }
    if (const auto* region = std::get_if<RegionLayerSpec>(&spec)) {
        return std::make_shared<RegionLayer>(BuildLayer(*region->layer, source, ejecta),
                                             BuildRegionShape(region->shape), region->feather,
                                             region->blend);
    }
    if (const auto* roughness = std::get_if<RoughnessLayerSpec>(&spec)) {
        return std::make_shared<RoughnessLayer>(roughness->roughness);
    }
    if (const auto* rocks = std::get_if<RockLayerSpec>(&spec)) {
        return std::make_shared<RockLayer>(
            rocks->rocks, rocks->rocks.ejecta.minCraterDiameter > 0.0 ? ejecta : nullptr);
    }
    const auto& craters = std::get<CraterLayerSpec>(spec);
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
    return std::make_shared<CraterLayer>(BuildCraterModels(craters), std::move(options));
}

std::string Number(double value) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.17g", value);
    return buffer.data();
}

}  // namespace

NoiseLayer NoiseLayerConfig::Effective() const {
    NoiseLayer effective = layer;
    if (hurst) {
        effective.fractal.gain = GainFromHurst(*hurst, layer.fractal.lacunarity);
    }
    return effective;
}

SyntheticTerrainParams NoiseLayerSpec::Params() const {
    return StackParams(stack);
}

CraterModels BuildCraterModels(const CraterLayerSpec& craters) {
    CraterModels models;
    models.density = BuildDensity(craters.density);
    models.ages = BuildAges(craters.ages, craters.density);
    models.degradation = BuildDegradation(craters.degradation, models.ages);
    models.profile = BuildProfile(craters.profile);
    return models;
}

namespace {

/// `DescribeSource` for the source at JSON `path`.
RasterGrid DescribeSourceAt(const SourceSpec& source, const std::filesystem::path& directory,
                            const std::string& path) {
    if (const auto* chain = std::get_if<ChainSourceSpec>(&source)) {
        if (chain->sources.empty()) {
            throw ConfigError(path + ".sources", "must not be empty");
        }
        RasterGrid grid;
        for (std::size_t n = 0; n < chain->sources.size(); ++n) {
            const RasterGrid described = DescribeSourceAt(
                chain->sources[n], directory, path + ".sources[" + std::to_string(n) + "]");
            if (n == 0) {
                grid = described;
            }
        }
        return grid;
    }
    if (const auto* flat = std::get_if<FlatSourceSpec>(&source)) {
        if (!(std::isfinite(flat->pixelSize) && flat->pixelSize > 0.0)) {
            throw ConfigError(path + ".pixel_size", "must be positive");
        }
        CheckGridSize(flat->width, flat->height, path + ".width");
        return FlatSource(flat->width, flat->height, flat->pixelSize, flat->elevation).Grid();
    }
    const auto& raster = std::get<RasterSourceSpec>(source);
    if (raster.path.empty()) {
        throw ConfigError(path + ".path", "must not be empty");
    }
    GeoTiffInfo info;
    try {
        info = ReadGeoTiffInfo(Resolve(raster.path, directory));
    } catch (const GeoTiffError& error) {
        throw ConfigError(path + ".path", error.what());
    }
    if (raster.band >= info.bands) {
        throw ConfigError(path + ".band", "the file has " + std::to_string(info.bands) +
                                              (info.bands == 1 ? " band" : " bands"));
    }
    if (info.geoKeys.IsGeographic()) {
        throw ConfigError(path + ".path",
                          "the raster's CRS is geographic (degrees); craters are sized in metres, "
                          "so reproject it first, for example with gdalwarp -t_srs");
    }
    RasterGrid grid{info.width, info.height, info.transform};
    if (raster.pixelSize) {
        if (!(std::isfinite(*raster.pixelSize) && *raster.pixelSize > 0.0)) {
            throw ConfigError(path + ".pixel_size", "must be positive");
        }
        if (grid.transform.rowRotation != 0.0 || grid.transform.colRotation != 0.0) {
            throw ConfigError(path + ".pixel_size", "rotated rasters cannot be resampled");
        }
        const double scaleX = std::fabs(grid.transform.pixelWidth) / *raster.pixelSize;
        const double scaleY = std::fabs(grid.transform.pixelHeight) / *raster.pixelSize;
        const double width = std::max(1.0, std::round(static_cast<double>(info.width) * scaleX));
        const double height = std::max(1.0, std::round(static_cast<double>(info.height) * scaleY));
        if (width > static_cast<double>(kMaxConfigGridSize) ||
            height > static_cast<double>(kMaxConfigGridSize)) {
            throw ConfigError(path + ".pixel_size", "resampling would exceed " +
                                                        std::to_string(kMaxConfigGridSize) +
                                                        " cells per side");
        }
        grid.width = static_cast<std::size_t>(width);
        grid.height = static_cast<std::size_t>(height);
        grid.transform.pixelWidth = std::copysign(*raster.pixelSize, grid.transform.pixelWidth);
        grid.transform.pixelHeight = std::copysign(*raster.pixelSize, grid.transform.pixelHeight);
    }
    CheckGridSize(grid.width, grid.height, path + (raster.pixelSize ? ".pixel_size" : ".path"));
    return grid;
}

std::shared_ptr<const Source> BuildSource(const SourceSpec& spec,
                                          const std::filesystem::path& directory) {
    if (const auto* flat = std::get_if<FlatSourceSpec>(&spec)) {
        return std::make_shared<FlatSource>(flat->width, flat->height, flat->pixelSize,
                                            flat->elevation);
    }
    if (const auto* chain = std::get_if<ChainSourceSpec>(&spec)) {
        std::vector<std::shared_ptr<const Source>> sources;
        for (const SourceSpec& source : chain->sources) {
            sources.push_back(BuildSource(source, directory));
        }
        return std::make_shared<SourceChain>(std::move(sources));
    }
    const auto& raster = std::get<RasterSourceSpec>(spec);
    return std::make_shared<GeoTiffSource>(Resolve(raster.path, directory), raster.band,
                                           raster.pixelSize, raster.resampling);
}

/// The raster source whose grid `source` covers, if any: itself, or a chain's first source.
const GeoTiffSource* GridRaster(const Source& source) {
    if (const auto* chain = dynamic_cast<const SourceChain*>(&source)) {
        return GridRaster(*chain->Sources().front());
    }
    return dynamic_cast<const GeoTiffSource*>(&source);
}

}  // namespace

RasterGrid DescribeSource(const SourceSpec& source, const std::filesystem::path& directory) {
    return DescribeSourceAt(source, directory, "source");
}

void ValidateConfig(const TerrainConfig& config, const std::filesystem::path& directory) {
    if (config.schemaVersion != kConfigSchemaVersion) {
        throw ConfigError("schema_version",
                          "version " + std::to_string(config.schemaVersion) + " is not supported");
    }
    const RasterGrid grid = DescribeSource(config.source, directory);
    bool craters = false;
    for (std::size_t i = 0; i < config.layers.size(); ++i) {
        ValidateLayer(config.layers[i], grid, "layers[" + std::to_string(i) + "]", craters);
    }
    for (std::size_t i = 0; i < config.filters.size(); ++i) {
        At("filters[" + std::to_string(i) + "]",
           [&] { ValidateRimSmoothing(std::get<RimSmoothing>(config.filters[i])); });
    }
    for (std::size_t i = 0; i < config.outputs.size(); ++i) {
        const std::string path = "outputs[" + std::to_string(i) + "]";
        std::visit(Overloaded{
                       [&](const CraterListOutput& output) {
                           if (output.path.empty()) {
                               throw ConfigError(path + ".path", "must not be empty");
                           }
                       },
                       [&](const HeightmapOutput& output) {
                           if (output.path.empty()) {
                               throw ConfigError(path + ".path", "must not be empty");
                           }
                           if (output.compression == TiffCompression::PackBits) {
                               throw ConfigError(path + ".compression",
                                                 "packbits is read-only; use none, lzw or deflate");
                           }
                           if (output.tileSize == 0 || output.tileSize % 16 != 0) {
                               throw ConfigError(path + ".tile_size",
                                                 "must be a positive multiple of 16");
                           }
                       },
                   },
                   config.outputs[i]);
    }
}

Terrain BuildTerrain(const TerrainConfig& config, const std::filesystem::path& directory) {
    ValidateConfig(config, directory);
    Terrain terrain;
    terrain.source = BuildSource(config.source, directory);
    std::shared_ptr<const CraterLayer> craters;
    for (const LayerSpec& layer : config.layers) {
        terrain.layers.push_back(BuildLayer(layer, *terrain.source, craters));
        if (auto built = std::dynamic_pointer_cast<const CraterLayer>(terrain.layers.back())) {
            craters = std::move(built);
        }
    }
    for (const FilterSpec& filter : config.filters) {
        terrain.filters.push_back(
            std::make_shared<RimSmoothingFilter>(std::get<RimSmoothing>(filter)));
    }
    return terrain;
}

GeneratedTerrain GenerateTerrain(const TerrainConfig& config,
                                 const std::filesystem::path& directory, unsigned threads) {
    const Terrain terrain = BuildTerrain(config, directory);
    const Stopwatch stopwatch;
    EvaluatedTerrain evaluated =
        Evaluate(terrain, terrain.source->Grid(), EvaluationOptions{0.0, threads});
    GeneratedTerrain generated{std::move(evaluated.heights), std::move(evaluated.features), {}};
    if (const GeoTiffSource* raster = GridRaster(*terrain.source)) {
        generated.geoKeys = raster->Keys();
    }
    LogInfo("pipeline", "Generated ", generated.heights.Width(), "x", generated.heights.Height(),
            " terrain with ", generated.features.craters.Size(), " craters and ",
            generated.features.rocks.size(), " rocks in ",
            FormatDuration(stopwatch.Milliseconds()));
    return generated;
}

void WriteCraterList(const CraterList& craters, const std::filesystem::path& path,
                     CraterListFormat format) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot write " + path.string());
    }
    if (format == CraterListFormat::Csv) {
        file << "x,y,diameter,age_gyr,id,manual\n";
        for (std::size_t n = 0; n < craters.Size(); ++n) {
            const Crater crater = craters.At(n);
            file << Number(crater.x) << ',' << Number(crater.y) << ',' << Number(crater.diameter)
                 << ',' << Number(crater.ageGyr) << ',' << crater.id << ','
                 << (crater.manual ? 1 : 0) << '\n';
        }
    } else {
        nlohmann::ordered_json json;
        json["craters"] = nlohmann::ordered_json::array();
        for (std::size_t n = 0; n < craters.Size(); ++n) {
            const Crater crater = craters.At(n);
            json["craters"].push_back({{"x", crater.x},
                                       {"y", crater.y},
                                       {"diameter", crater.diameter},
                                       {"age_gyr", crater.ageGyr},
                                       {"id", crater.id},
                                       {"manual", crater.manual}});
        }
        file << json.dump(2) << '\n';
    }
    if (!file) {
        throw std::runtime_error("Cannot write " + path.string());
    }
}

std::vector<std::filesystem::path> WriteOutputs(const TerrainConfig& config,
                                                const GeneratedTerrain& terrain,
                                                const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> written;
    for (const OutputSpec& spec : config.outputs) {
        const std::string& relative =
            std::visit([](const auto& output) -> const std::string& { return output.path; }, spec);
        const std::filesystem::path path = Resolve(relative, directory);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        if (const auto* heightmap = std::get_if<HeightmapOutput>(&spec)) {
            WriteGeoTiff(path, terrain.heights, terrain.geoKeys,
                         {.compression = heightmap->compression, .tileSize = heightmap->tileSize});
        } else {
            const auto& list = std::get<CraterListOutput>(spec);
            WriteCraterList(terrain.features.craters, path, list.format);
            LogInfo("pipeline", "Wrote ", terrain.features.craters.Size(), " craters to ",
                    path.string());
        }
        written.push_back(path);
    }
    return written;
}

}  // namespace terraingen
