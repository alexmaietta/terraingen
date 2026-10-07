#pragma once

#include "terraingen/raster/grid.h"
#include "terraingen/util/geometry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace terraingen {

/// A tile of a `TileScheme`: column `x` (east) and row `y` (south) of the tiles at `level`.
struct TileKey {
    int level = 0;
    std::int64_t x = 0;
    std::int64_t y = 0;

    bool operator==(const TileKey&) const = default;
};

/// Hashes a `TileKey`, for maps and caches.
struct TileKeyHash {
    std::size_t operator()(const TileKey& key) const;
};

/// A planar quadtree of north-up tiles: level 0 has square tiles `rootSize` world units on a side
/// with their corner grid at `origin` (the top-left corner of tile (0, 0)), and each level halves
/// the side, so tile (x, y) at level L has the four children (2x + i, 2y + j) at level L + 1. Every
/// tile has `cells` x `cells` samples, so the spacing halves with each level, as in a renderer's
/// level-of-detail quadtree. Tiles extend in every direction, with negative indices west and north
/// of the origin.
struct TileScheme {
    Vec2d origin{};
    double rootSize = 1.0;
    std::size_t cells = 256;

    /// A scheme whose level `level` has the cells of `grid`: the origin at the grid's top-left
    /// corner, and the root the smallest power-of-two multiple of `cells` grid cells covering the
    /// whole grid, so tiles at that level lie on the grid's lattice.
    /// @throws std::invalid_argument for an empty, rotated or not north-up grid, or zero `cells`.
    static TileScheme Covering(const RasterGrid& grid, std::size_t cells = 256);

    /// A longitude-latitude pyramid over a whole body, for terrains on a sphere: the origin at
    /// (-180, 90) and root tiles 180 degrees across, so level L has 2^(L+1) x 2^L tiles covering
    /// every longitude and latitude once (`BodyTiles`). Spacings are in degrees; a tile's spacing
    /// in metres is `RasterGrid::Spacing` on the sphere.
    /// @throws std::invalid_argument for zero `cells`.
    static TileScheme WholeBody(std::size_t cells = 256);

    /// Every tile of level `level` of a `WholeBody` scheme, row by row from the north-west.
    /// @throws std::invalid_argument for a negative level.
    static std::vector<TileKey> BodyTiles(int level);

    /// `key` with its column wrapped into [0, 2^(level+1)), the tile of a `WholeBody` scheme a
    /// neighbour across the antimeridian is.
    static TileKey WrapBodyTile(const TileKey& key);

    /// @throws std::invalid_argument for a non-positive or non-finite `rootSize`, a non-finite
    /// origin, or zero `cells`.
    void Validate() const;

    /// Side of a tile at `level`, in world units.
    double TileSize(int level) const;

    /// Sample spacing at `level`: the tile side over `cells`.
    double Spacing(int level) const;

    /// The coarsest level whose spacing is at most `spacing`, at least 0.
    /// @throws std::invalid_argument if `spacing` is not positive and finite.
    int LevelFor(double spacing) const;

    /// The tile at `level` containing (`x`, `y`); points on a shared edge belong to the tile east
    /// or south of it.
    TileKey TileAt(double x, double y, int level) const;

    /// The tiles at `level` that intersect `bounds`, row by row from the north-west.
    std::vector<TileKey> TilesCovering(const Bounds2d& bounds, int level) const;

    /// The tile's grid of `cells` x `cells` samples.
    /// @throws std::invalid_argument for a negative level.
    RasterGrid Grid(const TileKey& key) const;

    /// The tile one level coarser containing `key`.
    /// @throws std::invalid_argument for level 0.
    static TileKey Parent(const TileKey& key);

    /// The four tiles one level finer covering `key`: north-west, north-east, south-west,
    /// south-east.
    static std::array<TileKey, 4> Children(const TileKey& key);
};

}  // namespace terraingen
