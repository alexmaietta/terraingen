#pragma once

#include "settings.h"
#include "terraingen/core/raster_stats.h"

#include <array>
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

/// Detail texture paths being edited in the Surface panel and what happened when they loaded.
struct SurfacePanelState {
    std::array<char, 1024> albedoPath{};
    std::array<char, 1024> normalPath{};
    std::string albedoStatus;  ///< Size of the loaded texture, or why it failed to load.
    std::string normalStatus;
    bool hasAlbedoMap = false;  ///< Whether the renderer has an albedo map to toggle.
};

/// What the user asked for in the Surface panel this frame.
enum class SurfacePanelAction { None, LoadDetailAlbedo, LoadDetailNormal, ClearDetail };

/// Draws the Surface panel: reflectance model and its parameters, albedo, exposure and detail
/// textures, editing `material` in place.
SurfacePanelAction DrawSurfacePanel(SurfaceViewSettings& material, SurfacePanelState& state,
                                    const ernie::BrdfRegistry& registry);

/// Values shown in the Info panel.
struct InfoPanelData {
    std::string gpu;
    std::size_t width = 0;
    std::size_t height = 0;
    std::uint64_t triangles = 0;
    std::size_t patches = 0;
    std::uint32_t lodLevels = 0;
    std::uint64_t gpuBytes = 0;
    std::optional<RasterStats> stats;
    double generationMilliseconds = 0.0;
};

void DrawInfoPanel(const InfoPanelData& info);

}  // namespace terraingen::viewer
