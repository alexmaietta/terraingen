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

/// Values shown in the Info section.
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

/// Default and minimum widths of the sidebar, in pixels.
inline constexpr float kSidebarDefaultWidth = 420.0f;
inline constexpr float kSidebarMinWidth = 300.0f;

/// What the user asked for in the sidebar this frame.
struct SidebarActions {
    bool generate = false;  ///< Generate the terrain now.
    bool frame = false;     ///< Re-frame the camera on the terrain.
    SurfacePanelAction surface = SurfacePanelAction::None;
    float width = 0.0f;  ///< Current sidebar width in pixels.
};

/// Draws the sidebar: a full-height window docked on the left that can be widened by dragging its
/// right edge, with collapsible Info, Terrain (Surface and Craters), View and Surface sections.
/// Edits the settings in place.
SidebarActions DrawSidebar(TerrainSettings& settings, bool& autoRegenerate,
                           const TerrainPanelStatus& status, ViewSettings& view,
                           SurfacePanelState& surfaceState, const ernie::BrdfRegistry& registry,
                           const InfoPanelData& info);

/// Default and minimum heights of the Log panel, in pixels.
inline constexpr float kLogPanelDefaultHeight = 180.0f;
inline constexpr float kLogPanelMinHeight = 60.0f;

/// Filter settings of the Log panel and the records it last read.
struct LogPanelState {
    LogLevel minLevel = LogLevel::Info;
    std::array<char, 128> filter{};
    bool autoScroll = true;
    std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
    std::uint64_t version = ~std::uint64_t{0};
    std::vector<LogRecord> records;
};

/// Draws the Log panel docked along the bottom of the window from `left` pixels to the right
/// edge, with a resizable height, level and text filters, auto-scroll, Clear and Copy.
void DrawLogPanel(LogBuffer& buffer, LogPanelState& state, float left);

}  // namespace terraingen::viewer
