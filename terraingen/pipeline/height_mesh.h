/// @file
/// @brief `HeightMesh`, a grid of heights as a triangle mesh.
#pragma once

#include "terraingen/mesh/mesh.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <vector>

namespace terraingen {

/// What a `HeightMesh` leaves out and hangs below its edges.
struct HeightMeshOptions {
    /// Boxes in surface coordinates whose squares are left out: a square when all four of its
    /// cell centres lie in one, edges included.
    std::vector<Bounds2d> cutouts{};
    /// Depth in metres of a skirt hung below every boundary edge, around cutouts too; 0 for none.
    double skirt = 0.0;
};

/// A grid of heights as a mesh: a vertex at every cell centre with data, and two triangles facing
/// up for every square of four such vertices outside the cutouts, with a skirt facing out below
/// each boundary edge. Positions are in the frame boulder models are written in: world
/// coordinates and heights on a plane; on a sphere, metres east, north and up of `site` on the
/// datum sphere (as `PlacementAtSite`), so terrain and boulders line up. Skirts hang down the
/// frame's up axis.
/// @param site Surface coordinates of the site; unused on a plane.
/// @throws std::invalid_argument for a grid with fewer than four cells with data in a square
/// outside the cutouts, or a negative or non-finite skirt.
TriangleMesh HeightMesh(const Raster<float>& heights, const Surface& surface = {},
                        const Vec2d& site = {}, const HeightMeshOptions& options = {});

}  // namespace terraingen
