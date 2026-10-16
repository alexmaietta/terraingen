/// @file
/// @brief `HeightMesh`, a grid of heights as a triangle mesh.
#pragma once

#include "terraingen/mesh/mesh.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

namespace terraingen {

/// A grid of heights as a mesh: a vertex at every cell centre with data, and two triangles facing
/// up for every square of four such vertices. Positions are in the frame boulder models are
/// written in: world coordinates and heights on a plane; on a sphere, metres east, north and up
/// of `site` on the datum sphere (as `PlacementAtSite`), so terrain and boulders line up.
/// @param site Surface coordinates of the site; unused on a plane.
/// @throws std::invalid_argument for a grid with fewer than four cells with data in a square.
TriangleMesh HeightMesh(const Raster<float>& heights, const Surface& surface = {},
                        const Vec2d& site = {});

}  // namespace terraingen
