#include "terraingen/pipeline/run.h"

#include "terraingen/craters/ages/fresh.h"
#include "terraingen/craters/ages/steady_state.h"
#include "terraingen/craters/ages/uniform.h"
#include "terraingen/craters/degradation/freshness.h"
#include "terraingen/craters/degradation/none.h"
#include "terraingen/craters/filters.h"
#include "terraingen/craters/lunar/equilibrium.h"
#include "terraingen/craters/lunar/production.h"
#include "terraingen/craters/lunar/surface.h"
#include "terraingen/craters/profiles/martin_parkes_dunstan.h"
#include "terraingen/distribution/log_polynomial.h"
#include "terraingen/distribution/power_law.h"
#include "terraingen/distribution/spliced.h"
#include "terraingen/distribution/tabulated.h"
#include "terraingen/noise/kernels/layers.h"
#include "terraingen/pipeline/config_json.h"
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

/// Manual craters flagged and given stable IDs.
CraterList ManualCraters(const CraterConfig& craters) {
    CraterList list;
    for (Crater crater : craters.manual) {
        crater.manual = true;
        if (crater.id == 0) {
            crater.id = ManualCraterId(crater.x, crater.y, crater.diameter);
        }
        list.Append(crater);
    }
    return list;
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

SyntheticTerrainParams LayerParams(const std::vector<NoiseLayerConfig>& layers) {
    SyntheticTerrainParams params;
    params.layers.clear();
    for (const NoiseLayerConfig& layer : layers) {
        params.layers.push_back(layer.Effective());
    }
    return params;
}

/// Adds `layers` to every cell of `raster` with data, evaluated relative to the raster's origin.
void AddDetail(Raster<float>& raster, const std::vector<NoiseLayerConfig>& layers) {
    const SyntheticTerrainParams params = LayerParams(layers);
    const GeoTransform& transform = raster.Transform();
    for (std::size_t row = 0; row < raster.Height(); ++row) {
        for (std::size_t col = 0; col < raster.Width(); ++col) {
            float& cell = raster(col, row);
            if (raster.IsNoData(cell)) {
                continue;
            }
            const Vec2d centre = raster.CellCenter(col, row);
            cell += EvaluateNoiseLayers(static_cast<float>(centre.x - transform.originX),
                                        static_cast<float>(centre.y - transform.originY),
                                        params.layers.data(), params.layers.size());
        }
    }
}

void ValidateCraters(const CraterConfig& craters, const SurfaceGrid& grid) {
    for (std::size_t n = 0; n < craters.manual.size(); ++n) {
        const Crater& crater = craters.manual[n];
        const std::string path = "craters.manual[" + std::to_string(n) + "]";
        if (!(std::isfinite(crater.x) && std::isfinite(crater.y))) {
            throw ConfigError(path, "position must be finite");
        }
        if (!(std::isfinite(crater.diameter) && crater.diameter > 0.0)) {
            throw ConfigError(path + ".diameter", "must be positive");
        }
        if (!(std::isfinite(crater.ageGyr) && crater.ageGyr >= 0.0)) {
            throw ConfigError(path + ".age_gyr", "must not be negative");
        }
    }
    At("craters.spacing", [&] { ValidateCraterSpacing(craters.spacing); });
    At("craters.apply", [&] { ValidateCraterApplyOptions(craters.apply); });
    At("craters.rim_smoothing", [&] { ValidateRimSmoothing(craters.rimSmoothing); });
    const auto density = At("craters.density", [&] { return BuildDensity(craters.density); });
    const auto ages = At("craters.ages", [&] { return BuildAges(craters.ages, craters.density); });
    At("craters.degradation", [&] { return BuildDegradation(craters.degradation, ages); });
    At("craters.profile", [&] { return BuildProfile(craters.profile); });
    if (!craters.generate) {
        return;
    }
    if (!(std::isfinite(craters.minDiameter) && craters.minDiameter > 0.0)) {
        throw ConfigError("craters.min_diameter", "must be positive");
    }
    if (!(std::isfinite(craters.maxDiameter) && craters.maxDiameter > craters.minDiameter)) {
        throw ConfigError("craters.max_diameter", "must exceed min_diameter");
    }
    const double area = grid.Area();
    const double expected = At("craters.min_diameter", [&] {
        return density->ExpectedCount(area, craters.minDiameter, craters.maxDiameter);
    });
    const double limit = CraterGenerationOptions{}.maxExpectedCraters;
    if (expected > limit) {
        throw ConfigError("craters.min_diameter",
                          "about " + std::to_string(static_cast<long long>(expected)) +
                              " craters expected, more than the limit of " +
                              std::to_string(static_cast<long long>(limit)) +
                              "; raise min_diameter or set max_craters");
    }
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

SyntheticTerrainParams SyntheticSurfaceSpec::Params() const {
    return LayerParams(layers);
}

CraterModels BuildCraterModels(const CraterConfig& craters) {
    CraterModels models;
    models.density = BuildDensity(craters.density);
    models.ages = BuildAges(craters.ages, craters.density);
    models.degradation = BuildDegradation(craters.degradation, models.ages);
    models.profile = BuildProfile(craters.profile);
    return models;
}

Bounds2d SurfaceGrid::Bounds() const {
    constexpr double kInf = std::numeric_limits<double>::infinity();
    Bounds2d bounds{kInf, kInf, -kInf, -kInf};
    for (const double col : {0.0, static_cast<double>(width)}) {
        for (const double row : {0.0, static_cast<double>(height)}) {
            const Vec2d corner = transform.PixelToWorld(col, row);
            bounds.minX = std::fmin(bounds.minX, corner.x);
            bounds.minY = std::fmin(bounds.minY, corner.y);
            bounds.maxX = std::fmax(bounds.maxX, corner.x);
            bounds.maxY = std::fmax(bounds.maxY, corner.y);
        }
    }
    return bounds;
}

double SurfaceGrid::Area() const {
    return static_cast<double>(width) * static_cast<double>(height) * PixelSize() * PixelSize();
}

double SurfaceGrid::PixelSize() const {
    return std::sqrt(std::fabs(transform.pixelWidth * transform.pixelHeight -
                               transform.rowRotation * transform.colRotation));
}

SurfaceGrid DescribeSurface(const SurfaceSpec& surface, const std::filesystem::path& directory) {
    if (const auto* synthetic = std::get_if<SyntheticSurfaceSpec>(&surface)) {
        if (!(std::isfinite(synthetic->pixelSize) && synthetic->pixelSize > 0.0)) {
            throw ConfigError("surface.pixel_size", "must be positive");
        }
        CheckGridSize(synthetic->width, synthetic->height, "surface.width");
        const double halfX = 0.5 * static_cast<double>(synthetic->width) * synthetic->pixelSize;
        const double halfY = 0.5 * static_cast<double>(synthetic->height) * synthetic->pixelSize;
        return {synthetic->width, synthetic->height,
                GeoTransform::NorthUp(-halfX, halfY, synthetic->pixelSize)};
    }
    const auto& raster = std::get<RasterSurfaceSpec>(surface);
    if (raster.path.empty()) {
        throw ConfigError("surface.path", "must not be empty");
    }
    GeoTiffInfo info;
    try {
        info = ReadGeoTiffInfo(Resolve(raster.path, directory));
    } catch (const GeoTiffError& error) {
        throw ConfigError("surface.path", error.what());
    }
    if (raster.band >= info.bands) {
        throw ConfigError("surface.band", "the file has " + std::to_string(info.bands) +
                                              (info.bands == 1 ? " band" : " bands"));
    }
    if (info.geoKeys.IsGeographic()) {
        throw ConfigError("surface.path",
                          "the raster's CRS is geographic (degrees); craters are sized in metres, "
                          "so reproject it first, for example with gdalwarp -t_srs");
    }
    SurfaceGrid grid{info.width, info.height, info.transform};
    if (raster.pixelSize) {
        if (!(std::isfinite(*raster.pixelSize) && *raster.pixelSize > 0.0)) {
            throw ConfigError("surface.pixel_size", "must be positive");
        }
        if (grid.transform.rowRotation != 0.0 || grid.transform.colRotation != 0.0) {
            throw ConfigError("surface.pixel_size", "rotated rasters cannot be resampled");
        }
        const double scaleX = std::fabs(grid.transform.pixelWidth) / *raster.pixelSize;
        const double scaleY = std::fabs(grid.transform.pixelHeight) / *raster.pixelSize;
        const double width = std::max(1.0, std::round(static_cast<double>(info.width) * scaleX));
        const double height = std::max(1.0, std::round(static_cast<double>(info.height) * scaleY));
        if (width > static_cast<double>(kMaxConfigGridSize) ||
            height > static_cast<double>(kMaxConfigGridSize)) {
            throw ConfigError("surface.pixel_size", "resampling would exceed " +
                                                        std::to_string(kMaxConfigGridSize) +
                                                        " cells per side");
        }
        grid.width = static_cast<std::size_t>(width);
        grid.height = static_cast<std::size_t>(height);
        grid.transform.pixelWidth = std::copysign(*raster.pixelSize, grid.transform.pixelWidth);
        grid.transform.pixelHeight = std::copysign(*raster.pixelSize, grid.transform.pixelHeight);
    }
    CheckGridSize(grid.width, grid.height,
                  raster.pixelSize ? "surface.pixel_size" : "surface.path");
    return grid;
}

void ValidateConfig(const TerrainConfig& config, const std::filesystem::path& directory) {
    if (config.schemaVersion != kConfigSchemaVersion) {
        throw ConfigError("schema_version",
                          "version " + std::to_string(config.schemaVersion) + " is not supported");
    }
    const SurfaceGrid grid = DescribeSurface(config.surface, directory);
    if (const auto* synthetic = std::get_if<SyntheticSurfaceSpec>(&config.surface)) {
        ValidateLayers(synthetic->layers, "surface.layers", false);
    } else {
        ValidateLayers(std::get<RasterSurfaceSpec>(config.surface).detail, "surface.detail", true);
    }
    if (config.craters) {
        ValidateCraters(*config.craters, grid);
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

GeneratedTerrain GenerateTerrain(const TerrainConfig& config,
                                 const std::filesystem::path& directory) {
    ValidateConfig(config, directory);
    const Stopwatch stopwatch;
    GeneratedTerrain terrain;
    if (const auto* synthetic = std::get_if<SyntheticSurfaceSpec>(&config.surface)) {
        terrain.heights = GenerateSyntheticTerrain(synthetic->width, synthetic->height,
                                                   synthetic->pixelSize, synthetic->Params());
    } else {
        const auto& raster = std::get<RasterSurfaceSpec>(config.surface);
        GeoTiff tiff = ReadGeoTiff(Resolve(raster.path, directory), raster.band);
        terrain.heights = raster.pixelSize
                              ? Resample(tiff.raster, *raster.pixelSize, raster.resampling)
                              : std::move(tiff.raster);
        terrain.geoKeys = std::move(tiff.geoKeys);
        if (!raster.detail.empty()) {
            AddDetail(terrain.heights, raster.detail);
        }
    }
    const SurfaceGrid grid{terrain.heights.Width(), terrain.heights.Height(),
                           terrain.heights.Transform()};
    if (config.craters) {
        const CraterConfig& craters = *config.craters;
        const CraterModels models = BuildCraterModels(craters);
        const Bounds2d bounds = grid.Bounds();
        const CraterList manual = ManualCraters(craters);
        if (craters.generate) {
            terrain.craters =
                GenerateCraters(PlanarCells(), bounds, *models.density, *models.ages, craters.seed,
                                {.dMin = craters.minDiameter,
                                 .dMax = craters.maxDiameter,
                                 .marginRadii = models.profile->ReachRadii(),
                                 .spacing = craters.spacing,
                                 .manual = manual,
                                 .maxCraters = craters.maxCraters});
        } else {
            for (std::size_t n = 0; n < manual.Size(); ++n) {
                const Crater crater = manual.At(n);
                const double margin = 0.5 * models.profile->ReachRadii() * crater.diameter;
                if (bounds.Expanded(margin).Contains(crater.x, crater.y)) {
                    terrain.craters.Append(crater);
                }
            }
        }
        ApplyCraters(terrain.heights, terrain.craters, *models.profile, *models.degradation,
                     craters.apply);
        SmoothCraterRims(terrain.heights, terrain.craters, craters.rimSmoothing);
    }
    LogInfo("pipeline", "Generated ", grid.width, "x", grid.height, " terrain with ",
            terrain.craters.Size(), " craters in ", FormatDuration(stopwatch.Milliseconds()));
    return terrain;
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
            WriteCraterList(terrain.craters, path, list.format);
            LogInfo("pipeline", "Wrote ", terrain.craters.Size(), " craters to ", path.string());
        }
        written.push_back(path);
    }
    return written;
}

}  // namespace terraingen
