// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `BuildMesh`, a grid of heights as a triangle mesh, and `FillMesh` into caller buffers.
#pragma once

#include "terraingen/geometry/geometry.h"
#include "terraingen/geometry/surface.h"
#include "terraingen/mesh/mesh.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/span.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace terraingen {

/// What a `BuildMesh` leaves out and hangs below its edges.
struct BuildMeshOptions {
    /// Boxes in surface coordinates whose squares are left out: a square when all four of its
    /// cell centers lie in one, edges included.
    std::vector<Bounds2d> cutouts{};
    /// Depth in meters of a skirt hung below every boundary edge, around cutouts too; 0 for none.
    double skirt = 0.0;
};

/// The number of vertices and triangles of a mesh.
struct MeshSize {
    std::size_t vertices = 0;   ///< Vertices, skirt vertices included.
    std::size_t triangles = 0;  ///< Triangles, skirt triangles included.

    /// Memberwise equality.
    bool operator==(const MeshSize& other) const {
        return vertices == other.vertices && triangles == other.triangles;
    }
    /// Memberwise inequality.
    bool operator!=(const MeshSize& other) const {
        return !(*this == other);
    }
};

/// A grid of heights as a mesh: a vertex at every cell center with data, and two triangles facing
/// up for every square of four such vertices outside the cutouts, with a skirt facing out below
/// each boundary edge. Positions are in the frame boulder models are written in: world
/// coordinates and heights on a plane; on a sphere, meters east, north and up of `site` on the
/// datum sphere (as `PlacementAtSite`), so terrain and boulders line up. Skirts hang down the
/// frame's up axis. Made with `GetMeshSize` and `FillMesh`.
/// @param site Surface coordinates of the site; unused on a plane.
/// @throws std::invalid_argument as `FillMesh`.
TriangleMesh BuildMesh(const Raster<float>& heights, const Surface& surface = {},
                       const Vec2d& site = {}, const BuildMeshOptions& options = {});

/// The number of vertices and triangles `BuildMesh` makes of `heights`: cells with data, squares
/// outside the cutouts and the skirt.
/// @throws std::invalid_argument for a grid with fewer than four cells with data in a square
/// outside the cutouts, or a negative or non-finite skirt.
MeshSize GetMeshSize(const Raster<float>& heights, const BuildMeshOptions& options = {});

/// Writes the mesh `BuildMesh` makes of `heights` into caller buffers, bit for bit: `positions`
/// as x, y, z triples and `triangles` as vertex index triples, in `BuildMesh`'s order. The layout
/// matches an array of Chrono's ChVector3d and ChVector3i, or NumPy arrays of shape (N, 3) float64
/// and (M, 3) int32, so callers resize those to `GetMeshSize` and pass their data with no copy.
/// @param positions 3 * `GetMeshSize(heights, options).vertices` doubles, in meters.
/// @param triangles 3 * `GetMeshSize(heights, options).triangles` indices into the vertices.
/// @param site Surface coordinates of the site; unused on a plane.
/// @throws std::invalid_argument as `GetMeshSize`, for a buffer of another size, or for a mesh of
/// more than INT32_MAX vertices.
void FillMesh(const Raster<float>& heights, Span<double> positions, Span<std::int32_t> triangles,
              const Surface& surface = {}, const Vec2d& site = {},
              const BuildMeshOptions& options = {});

}  // namespace terraingen
