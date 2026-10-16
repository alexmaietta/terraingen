/// @file
/// @brief Quadtree tile schemes: planar tiles and longitude-latitude tiles over a whole body.
#pragma once

#include "terraingen/raster/grid.h"
#include "terraingen/util/geometry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace terraingen {

/// @brief A tile of a `TileScheme`: column `x` (east) and row `y` (south) of the tiles at `level`.
struct TileKey {
    int level = 0;
    std::int64_t x = 0;
    std::int64_t y = 0;

    bool operator==(const TileKey&) const = default;
};

/// @brief Hashes a `TileKey`, for maps and caches.
struct TileKeyHash {
    /// @brief The key's hash.
    /// @param key The tile.
    /// @return A hash of its level, column and row.
    std::size_t operator()(const TileKey& key) const;
};

/// @brief A quadtree of north-up tiles, as in a renderer's level-of-detail quadtree.
///
/// Level 0 has square tiles `rootSize` world units on a side, tile (0, 0) with its top-left corner
/// at `origin`; each level halves the side, so tile (x, y) at level L has the children
/// (2x + i, 2y + j) at level L + 1. Every tile has `cells` x `cells` samples, so the spacing halves
/// with each level. Tiles extend in every direction, negative indices west and north of the origin.
/// Levels run from 0 to 60.
struct TileScheme {
    Vec2d origin{};           ///< Top-left corner of tile (0, 0), in world units.
    double rootSize = 1.0;    ///< Side of a level-0 tile, in world units.
    std::size_t cells = 256;  ///< Samples along each side of a tile.

    /// @brief A scheme covering a grid, one of whose levels has the grid's cell size: the origin
    /// at the grid's top-left corner, and the root the smallest power-of-two multiple of `cells`
    /// grid cells covering the whole grid, so tiles at that level lie on the grid's lattice.
    /// @param grid A north-up grid with square cells.
    /// @param cells Samples along each side of a tile.
    /// @return The scheme.
    /// @throws std::invalid_argument for an empty, rotated or not north-up grid, cells that are not
    /// square, or zero `cells`.
    static TileScheme Covering(const RasterGrid& grid, std::size_t cells = 256);

    /// @brief A longitude-latitude pyramid over a whole body, for terrains on a sphere: the origin
    /// at (-180, 90) and root tiles 180 degrees across, so level L has 2^(L+1) x 2^L tiles covering
    /// every longitude and latitude once (`BodyTiles`). Spacings are in degrees; a tile's spacing
    /// in metres is `RasterGrid::Spacing` on the sphere.
    /// @param cells Samples along each side of a tile.
    /// @return The scheme.
    /// @throws std::invalid_argument for zero `cells`.
    static TileScheme WholeBody(std::size_t cells = 256);

    /// @brief Every tile of a level of a `WholeBody` scheme.
    /// @param level The level.
    /// @return The tiles, row by row from the north-west.
    /// @throws std::invalid_argument for a level outside [0, 30].
    static std::vector<TileKey> BodyTiles(int level);

    /// @brief The tile of a `WholeBody` scheme that a neighbour across the antimeridian is.
    /// @param key A tile, its column possibly out of range.
    /// @return `key` with its column wrapped into [0, 2^(level+1)).
    /// @throws std::invalid_argument for a level outside [0, 60].
    static TileKey WrapBodyTile(const TileKey& key);

    /// @brief Checks the scheme.
    /// @throws std::invalid_argument for a non-positive or non-finite `rootSize`, a non-finite
    /// origin, or zero `cells`.
    void Validate() const;

    /// @brief The side of a tile.
    /// @param level The tile's level.
    /// @return The side, in world units.
    /// @throws std::invalid_argument for a level outside [0, 60].
    double TileSize(int level) const;

    /// @brief The sample spacing of a level: the tile side over `cells`.
    /// @param level The level.
    /// @return The spacing, in world units.
    /// @throws std::invalid_argument for a level outside [0, 60].
    double Spacing(int level) const;

    /// @brief The coarsest level whose spacing is at most a given spacing.
    /// @param spacing The spacing, in world units.
    /// @return The level, from 0 to 60.
    /// @throws std::invalid_argument if `spacing` is not positive and finite.
    int LevelFor(double spacing) const;

    /// @brief The tile containing a point; points on a shared edge belong to the tile east or south
    /// of it.
    /// @param x The point's x, in world units.
    /// @param y The point's y, in world units.
    /// @param level The tile's level.
    /// @return The tile.
    /// @throws std::invalid_argument for a level outside [0, 60].
    TileKey TileAt(double x, double y, int level) const;

    /// @brief The tiles that intersect a box.
    /// @param bounds The box, in world units.
    /// @param level The tiles' level.
    /// @return The tiles, row by row from the north-west.
    /// @throws std::invalid_argument for a level outside [0, 60].
    std::vector<TileKey> TilesCovering(const Bounds2d& bounds, int level) const;

    /// @brief A tile's grid of `cells` x `cells` samples.
    /// @param key The tile.
    /// @return Its grid.
    /// @throws std::invalid_argument for a level outside [0, 60].
    RasterGrid Grid(const TileKey& key) const;

    /// @brief The tile one level coarser containing a tile.
    /// @param key The tile.
    /// @return Its parent.
    /// @throws std::invalid_argument for level 0 or a level above 60.
    static TileKey Parent(const TileKey& key);

    /// @brief The four tiles one level finer covering a tile.
    /// @param key The tile.
    /// @return Its children: north-west, north-east, south-west, south-east.
    /// @throws std::invalid_argument for a level outside [0, 59].
    static std::array<TileKey, 4> Children(const TileKey& key);
};

}  // namespace terraingen
