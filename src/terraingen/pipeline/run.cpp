#include "terraingen/pipeline/run.h"

#include "terraingen/core/log.h"
#include "terraingen/distributions/log_polynomial.h"
#include "terraingen/distributions/power_law.h"
#include "terraingen/distributions/spliced.h"
#include "terraingen/distributions/tabulated.h"
#include "terraingen/features/craters/ages/fresh.h"
#include "terraingen/features/craters/ages/steady_state.h"
#include "terraingen/features/craters/ages/uniform.h"
#include "terraingen/features/craters/degradation/freshness.h"
#include "terraingen/features/craters/degradation/none.h"
#include "terraingen/features/craters/filters.h"
#include "terraingen/features/craters/lunar/equilibrium.h"
#include "terraingen/features/craters/lunar/production.h"
#include "terraingen/features/craters/lunar/surface.h"
#include "terraingen/features/craters/profiles/martin_parkes_dunstan.h"
#include "terraingen/pipeline/config_json.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
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

void ValidateCraters(const CraterConfig& craters, const GridConfig& grid) {
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
    const Bounds2d bounds = GridBounds(grid);
    const double area = (bounds.maxX - bounds.minX) * (bounds.maxY - bounds.minY);
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

SyntheticTerrainParams SurfaceConfig::Params() const {
    SyntheticTerrainParams params;
    params.layers.clear();
    for (const NoiseLayerConfig& layer : layers) {
        params.layers.push_back(layer.Effective());
    }
    return params;
}

CraterModels BuildCraterModels(const CraterConfig& craters) {
    CraterModels models;
    models.density = BuildDensity(craters.density);
    models.ages = BuildAges(craters.ages, craters.density);
    models.degradation = BuildDegradation(craters.degradation, models.ages);
    models.profile = BuildProfile(craters.profile);
    return models;
}

Bounds2d GridBounds(const GridConfig& grid) {
    const double halfX = 0.5 * static_cast<double>(grid.width) * grid.pixelSize;
    const double halfY = 0.5 * static_cast<double>(grid.height) * grid.pixelSize;
    return {-halfX, -halfY, halfX, halfY};
}

void ValidateConfig(const TerrainConfig& config) {
    if (config.schemaVersion != kConfigSchemaVersion) {
        throw ConfigError("schema_version",
                          "version " + std::to_string(config.schemaVersion) + " is not supported");
    }
    const GridConfig& grid = config.grid;
    for (const auto& [name, size] :
         {std::pair{"grid.width", grid.width}, std::pair{"grid.height", grid.height}}) {
        if (size < 2 || size > kMaxConfigGridSize) {
            throw ConfigError(name, "must be between 2 and " + std::to_string(kMaxConfigGridSize));
        }
    }
    if (!(std::isfinite(grid.pixelSize) && grid.pixelSize > 0.0)) {
        throw ConfigError("grid.pixel_size", "must be positive");
    }
    const auto& layers = config.surface.layers;
    if (layers.empty() || layers.size() > kMaxNoiseLayers) {
        throw ConfigError("surface.layers",
                          "needs 1 to " + std::to_string(kMaxNoiseLayers) + " layers");
    }
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const std::string path = "surface.layers[" + std::to_string(i) + "]";
        if (layers[i].hurst && !std::isfinite(*layers[i].hurst)) {
            throw ConfigError(path + ".hurst", "must be finite");
        }
        At(path, [&] { ValidateNoiseLayer(layers[i].Effective()); });
    }
    if (config.craters) {
        ValidateCraters(*config.craters, grid);
    }
    for (std::size_t i = 0; i < config.outputs.size(); ++i) {
        const auto& output = std::get<CraterListOutput>(config.outputs[i]);
        if (output.path.empty()) {
            throw ConfigError("outputs[" + std::to_string(i) + "].path", "must not be empty");
        }
    }
}

GeneratedTerrain GenerateTerrain(const TerrainConfig& config) {
    ValidateConfig(config);
    const Stopwatch stopwatch;
    const GridConfig& grid = config.grid;
    GeneratedTerrain terrain{
        GenerateSyntheticTerrain(grid.width, grid.height, grid.pixelSize, config.surface.Params()),
        {}};
    if (config.craters) {
        const CraterConfig& craters = *config.craters;
        const CraterModels models = BuildCraterModels(craters);
        const Bounds2d bounds = GridBounds(grid);
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
        const auto& output = std::get<CraterListOutput>(spec);
        std::filesystem::path path = output.path;
        if (path.is_relative()) {
            path = directory / path;
        }
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        WriteCraterList(terrain.craters, path, output.format);
        LogInfo("pipeline", "Wrote ", terrain.craters.Size(), " craters to ", path.string());
        written.push_back(path);
    }
    return written;
}

}  // namespace terraingen
