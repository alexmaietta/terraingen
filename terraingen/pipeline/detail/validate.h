/// @file
/// @brief Internal: checks of a configuration's albedo, shared by `ValidateConfig` and
/// `BuildAlbedo`.
#pragma once

#include "terraingen/pipeline/config.h"
#include "terraingen/util/surface.h"

#include <filesystem>

namespace terraingen {

/// @brief Checks a configuration's albedo against its crater layers and files.
/// @param config A configuration with an albedo.
/// @param directory Folder relative input paths resolve against.
/// @param surface The configuration's surface.
/// @throws ConfigError naming the first invalid value under "albedo".
void ValidateAlbedo(const TerrainConfig& config, const std::filesystem::path& directory,
                    const Surface& surface);

}  // namespace terraingen
