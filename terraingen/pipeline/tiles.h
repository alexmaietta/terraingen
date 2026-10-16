/// @file
/// @brief Quadtree tile schemes (planar tiles and longitude-latitude tiles over a whole body),
/// tiles of a terrain evaluated on them, and a cache of those tiles.
#pragma once

#include "terraingen/pipeline/terrain.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/lru_cache.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
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

/// @brief One tile of a scheme at its level's spacing, as a piece of the unbounded terrain.
///
/// The tile is evaluated with the filters' margin around it and cropped, so it equals the same
/// cells of any larger grid on the same lattice (to the bit where cell centres coincide exactly,
/// such as with power-of-two spacings). The features are those placed while evaluating, margin
/// included.
/// @param terrain The terrain.
/// @param scheme The tile scheme.
/// @param key The tile.
/// @param threads Threads to use; 0 for every hardware thread.
/// @return The tile's heights and features.
/// @throws std::invalid_argument for an invalid scheme, level or terrain; and whatever the parts
/// throw.
EvaluatedTerrain EvaluateTile(const Terrain& terrain, const TileScheme& scheme, const TileKey& key,
                              unsigned threads = 1);

/// @brief `EvaluateTile` for several tiles, split over threads.
/// @param terrain The terrain.
/// @param scheme The tile scheme.
/// @param keys The tiles.
/// @param threads Threads to use; 0 for every hardware thread.
/// @return The tiles, in the order of `keys`.
/// @throws std::invalid_argument as `EvaluateTile`.
std::vector<EvaluatedTerrain> EvaluateTiles(const Terrain& terrain, const TileScheme& scheme,
                                            const std::vector<TileKey>& keys, unsigned threads = 0);

/// @brief Tiles of one terrain and scheme, evaluated on demand with `EvaluateTile` and kept up to
/// a byte budget, least recently used first out. Safe to use from several threads at once; tiles
/// are re-evaluated identically, so eviction never changes results.
class TileCache {
  public:
    /// @brief A cache of a terrain's tiles.
    /// @param terrain The terrain.
    /// @param scheme The tile scheme.
    /// @param maxBytes The byte budget.
    /// @throws std::invalid_argument for an invalid scheme or terrain, or a zero `maxBytes`.
    TileCache(Terrain terrain, TileScheme scheme, std::size_t maxBytes = std::size_t{1} << 30);

    /// @brief A tile, evaluated on one thread when it is not held.
    /// @param key The tile.
    /// @return The evaluated tile.
    /// @throws std::invalid_argument for a level outside the scheme's; and whatever the terrain's
    /// parts throw.
    std::shared_ptr<const EvaluatedTerrain> Get(const TileKey& key);

    /// @brief Bytes held: four per height plus the features' storage.
    /// @return The bytes held.
    std::size_t Size() const {
        return cache_.Size();
    }

    /// @brief The byte budget.
    /// @return The budget given at construction.
    std::size_t MaxBytes() const {
        return cache_.Capacity();
    }

    /// @brief Tiles evicted since construction.
    /// @return The eviction count.
    std::size_t Evictions() const {
        return cache_.Evictions();
    }

    /// @brief The tile scheme.
    /// @return The scheme given at construction.
    const TileScheme& Scheme() const {
        return scheme_;
    }

  private:
    Terrain terrain_;
    TileScheme scheme_;
    LruCache<TileKey, EvaluatedTerrain, TileKeyHash> cache_;
};

}  // namespace terraingen
