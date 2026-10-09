#pragma once

#include "terraingen/raster/raster.h"

#include <ernie/scene/path.h>
#include <ernie/scene/scene.h>

namespace terraingen::gui {

/// The prims `ShowTerrain` and `SetUpScene` define.
inline const ernie::Path kTerrainPrim{"/World/Terrain"};
inline const ernie::Path kMaterialPrim{"/Looks/Terrain"};
inline const ernie::Path kSunPrim{"/World/Sun"};
inline const ernie::Path kSettingsPrim{"/Render/Settings"};

/// Defines what a TerrainGen scene needs besides its terrain, keeping prims already there: a
/// sun 30 degrees up in the north-east, the terrain's grey material, and render settings that
/// shade it with an elevation colormap.
void SetUpScene(ernie::Scene& scene);

/// Writes `heights` as the scene's Terrain prim (`kTerrainPrim`), bound to `kMaterialPrim`, and
/// `albedo`, when given, as that material's albedo map; or clears the map. Rasters are copied.
/// @throws std::invalid_argument for a raster that is empty, rotated, not north-up, without
/// square cells or with angular (geographic) coordinates, or an albedo raster on another grid.
void ShowTerrain(ernie::Scene& scene, const Raster<float>& heights, bool geographic = false,
                 const Raster<float>* albedo = nullptr);

}  // namespace terraingen::gui
