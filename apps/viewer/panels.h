#pragma once

#include "log_buffer.h"
#include "settings.h"
#include "terraingen/raster/raster_stats.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace terraingen::viewer {

/// Generation status shown in the Terrain section.
struct TerrainPanelStatus {
    bool generating = false;
    std::string error;
};

/// Detail texture paths being edited in the Surface section and what happened when they loaded.
struct SurfacePanelState {
    std::array<char, 1024> albedoPath{};
    std::array<char, 1024> normalPath{};
    std::string albedoStatus;  ///< Size of the loaded texture, or why it failed to load.
    std::string normalStatus;
    bool hasAlbedoMap = false;  ///< Whether the renderer has an albedo map to toggle.
};

/// What the user asked for in the Surface section this frame.
enum class SurfacePanelAction { None, LoadDetailAlbedo, LoadDetailNormal, ClearDetail };

/// Values shown in the Info panel; the viewer's Stats panel shows the renderer's.
struct InfoPanelData {
    std::size_t width = 0;
    std::size_t height = 0;
    std::optional<RasterStats> stats;
    double generationMilliseconds = 0.0;
};

/// Draws the Terrain panel's contents: generation controls, then the Surface and Craters
/// groups. Edits `settings` in place; returns whether to generate now.
bool DrawTerrainPanel(TerrainSettings& settings, bool& autoRegenerate,
                      const TerrainPanelStatus& status);

/// Draws the View panel's contents: sun, shading, shadows, level of detail and camera exposure.
/// Returns whether to frame the terrain.
bool DrawViewPanel(ViewSettings& view);

/// Draws the Surface panel's contents: reflectance model and parameters, albedo and detail
/// textures. Returns what the user asked for.
SurfacePanelAction DrawSurfacePanel(SurfaceViewSettings& material, SurfacePanelState& state,
                                    const ernie::BrdfRegistry& registry);

/// Draws the Info panel's contents: version, grid, height range, generation time and controls.
void DrawInfoPanel(const InfoPanelData& info);

/// Filter settings of the Log panel and the records it last read.
struct LogPanelState {
    LogLevel minLevel = LogLevel::Info;
    std::array<char, 128> filter{};
    bool autoScroll = true;
    std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
    std::uint64_t version = ~std::uint64_t{0};
    std::vector<LogRecord> records;
};

/// Draws the Log panel's contents: level and text filters, auto-scroll, Clear and Copy, and the
/// records.
void DrawLogPanel(LogBuffer& buffer, LogPanelState& state);

}  // namespace terraingen::viewer
