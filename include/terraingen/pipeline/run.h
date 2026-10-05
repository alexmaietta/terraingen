#pragma once

#include "terraingen/distributions/size_frequency.h"
#include "terraingen/features/craters/ages/age_model.h"
#include "terraingen/features/craters/degradation/degradation_model.h"
#include "terraingen/features/craters/population.h"
#include "terraingen/features/craters/profiles/profile.h"
#include "terraingen/pipeline/config.h"
#include "terraingen/raster/raster.h"
#include "terraingen/spatial/cells.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace terraingen {

/// Largest grid side a configuration may request.
inline constexpr std::size_t kMaxConfigGridSize = 65536;

/// The library models a `CraterConfig` selects.
struct CraterModels {
    std::shared_ptr<const SizeFrequencyDistribution> density;
    std::shared_ptr<const CraterAgeModel> ages;
    std::shared_ptr<const DegradationModel> degradation;
    std::shared_ptr<const CraterProfile> profile;
};

/// Builds the models `craters` selects.
/// @throws std::invalid_argument or std::domain_error for invalid model parameters.
CraterModels BuildCraterModels(const CraterConfig& craters);

/// The region a configuration's grid covers, centred on the origin.
Bounds2d GridBounds(const GridConfig& grid);

/// Checks everything that parsing cannot: grid limits, noise layer parameters, model parameters,
/// crater options and the expected crater count against `CraterGenerationOptions` limits.
/// @throws ConfigError naming the first invalid value by its JSON path.
void ValidateConfig(const TerrainConfig& config);

/// A generated terrain: heights and the craters applied to them.
struct GeneratedTerrain {
    Raster<float> heights;
    CraterList craters;
};

/// Generates the terrain `config` describes: the noise surface, then the craters generated,
/// applied and rim-smoothed with the configured models and options.
/// @throws ConfigError if the configuration is invalid.
GeneratedTerrain GenerateTerrain(const TerrainConfig& config);

/// Writes `craters` as CSV (columns x, y, diameter, age_gyr, id, manual) or as JSON ({"craters":
/// [{"x": ..., "y": ..., ...}]}), with doubles written exactly.
/// @throws std::runtime_error if the file cannot be written.
void WriteCraterList(const CraterList& craters, const std::filesystem::path& path,
                     CraterListFormat format);

/// Writes `config`'s outputs, resolving relative paths against `directory`, and returns the paths
/// written.
/// @throws std::runtime_error if an output cannot be written.
std::vector<std::filesystem::path> WriteOutputs(const TerrainConfig& config,
                                                const GeneratedTerrain& terrain,
                                                const std::filesystem::path& directory);

}  // namespace terraingen
