/// @file
/// @brief The GUI's scene: its prims, lighting and settings, and terrain heights shown in it.
#pragma once

#include "terraingen/raster/raster.h"

#include <ernie/scene/path.h>
#include <ernie/scene/scene.h>

namespace terraingen::gui {

/// The terrain prim `ShowTerrain` writes.
inline const ernie::Path kTerrainPrim{"/World/Terrain"};
/// The terrain's material prim.
inline const ernie::Path kMaterialPrim{"/Looks/Terrain"};
/// The sun prim `SetUpScene` defines.
inline const ernie::Path kSunPrim{"/World/Sun"};
/// The render settings prim `SetUpScene` defines.
inline const ernie::Path kSettingsPrim{"/Render/Settings"};

/// Defines what a TerrainGen scene needs besides its terrain, keeping prims already there:
/// a sun 30 degrees up in the north-east, the terrain's gray material, and render settings that
/// shade it with an elevation colormap.
void SetUpScene(ernie::Scene& scene);

/// Writes heights as the scene's terrain prim (`kTerrainPrim`), bound to `kMaterialPrim`,
/// and an albedo as that material's albedo map, or clears the map. Rasters are copied.
/// @param heights The heights, north-up with square cells.
/// @param geographic Whether the heights are in longitude and latitude, which cannot be shown yet.
/// @param albedo The albedo on the heights' grid; null or empty clears the map.
/// @throws std::invalid_argument for geographic heights, a raster with fewer than 2 x 2 cells,
/// rotated, not north-up or without square cells, or an albedo on another grid.
void ShowTerrain(ernie::Scene& scene, const Raster<float>& heights, bool geographic = false,
                 const Raster<float>* albedo = nullptr);

}  // namespace terraingen::gui
