#pragma once

#include "terrain_settings.h"
#include "terraingen/core/raster_stats.h"
#include "view_settings.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace terraingen::viewer {

/// Generation status shown in the Terrain panel.
struct TerrainPanelStatus {
    bool generating = false;
    std::string error;
};

/// Draws the Terrain panel, editing `settings` in place.
/// @return True when the user asked to generate.
bool DrawTerrainPanel(TerrainSettings& settings, bool& autoRegenerate,
                      const TerrainPanelStatus& status);

/// Draws the View panel, editing `view` in place.
/// @return True when the user asked to re-frame the camera.
bool DrawViewPanel(ViewSettings& view);

/// Values shown in the Info panel.
struct InfoPanelData {
    std::string gpu;
    std::size_t width = 0;
    std::size_t height = 0;
    std::uint64_t triangles = 0;
    std::uint64_t gpuBytes = 0;
    std::optional<RasterStats> stats;
    double generationMilliseconds = 0.0;
};

void DrawInfoPanel(const InfoPanelData& info);

}  // namespace terraingen::viewer
