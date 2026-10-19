/// @file
/// @brief Quadtree tile schemes, terrain tiles evaluated on them, and a cache of those tiles.
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

/// A tile of a `TileScheme`: column `x` (east) and row `y` (south) of the tiles at `level`.
struct TileKey {
    int level = 0;
    std::int64_t x = 0;
    std::int64_t y = 0;

    bool operator==(const TileKey& other) const {
        return level == other.level && x == other.x && y == other.y;
    }
    bool operator!=(const TileKey& other) const {
        return !(*this == other);
    }
};

/// Hashes a `TileKey`, for maps and caches.
struct TileKeyHash {
    /// A hash of the key's level, column and row.
    std::size_t operator()(const TileKey& key) const;
};

/// A quadtree of north-up tiles, as in a renderer's level-of-detail quadtree.
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

    /// A scheme covering a north-up grid with square cells, one of whose levels has the grid's
    /// cell size: the origin at the grid's top-left corner, and the root the smallest
    /// power-of-two multiple of `cells` grid cells covering the whole grid, so tiles at that level
    /// lie on the grid's lattice.
    /// @throws std::invalid_argument for an empty, rotated or not north-up grid, cells that are not
    /// square, or zero `cells`.
    static TileScheme Covering(const RasterGrid& grid, std::size_t cells = 256);

    /// A longitude-latitude pyramid over a whole body, for terrains on a sphere: the origin
    /// at (-180, 90) and root tiles 180 degrees across, so level L has 2^(L+1) x 2^L tiles covering
    /// every longitude and latitude once (`BodyTiles`). Spacings are in degrees; a tile's spacing
    /// in metres is `RasterGrid::Spacing` on the sphere.
    /// @throws std::invalid_argument for zero `cells`.
    static TileScheme WholeBody(std::size_t cells = 256);

    /// Every tile of `level` of a `WholeBody` scheme, row by row from the north-west.
    /// @throws std::invalid_argument for a level outside [0, 30].
    static std::vector<TileKey> BodyTiles(int level);

    /// `key`, a tile of a `WholeBody` scheme, with its column wrapped across the antimeridian into
    /// [0, 2^(level+1)).
    /// @throws std::invalid_argument for a level outside [0, 60].
    static TileKey WrapBodyTile(const TileKey& key);

    /// Checks the scheme.
    /// @throws std::invalid_argument for a non-positive or non-finite `rootSize`, a non-finite
    /// origin, or zero `cells`.
    void Validate() const;

    /// The side of a tile at `level`, in world units.
    /// @throws std::invalid_argument for a level outside [0, 60].
    double TileSize(int level) const;

    /// The sample spacing of `level` in world units: the tile side over `cells`.
    /// @throws std::invalid_argument for a level outside [0, 60].
    double Spacing(int level) const;

    /// The coarsest level, from 0 to 60, whose spacing is at most `spacing` world units.
    /// @throws std::invalid_argument if `spacing` is not positive and finite.
    int ResolvingLevel(double spacing) const;

    /// The tile at `level` containing world point (`x`, `y`); points on a shared edge belong to the
    /// tile east or south of it.
    /// @throws std::invalid_argument for a level outside [0, 60].
    TileKey TileAt(double x, double y, int level) const;

    /// The tiles at `level` that intersect `bounds` (world units), row by row from the north-west.
    /// @throws std::invalid_argument for a level outside [0, 60].
    std::vector<TileKey> TilesCovering(const Bounds2d& bounds, int level) const;

    /// A tile's grid of `cells` x `cells` samples.
    /// @throws std::invalid_argument for a level outside [0, 60].
    RasterGrid Grid(const TileKey& key) const;

    /// The tile one level coarser containing `key`.
    /// @throws std::invalid_argument for level 0 or a level above 60.
    static TileKey Parent(const TileKey& key);

    /// The four tiles one level finer covering `key`: north-west, north-east, south-west,
    /// south-east.
    /// @throws std::invalid_argument for a level outside [0, 59].
    static std::array<TileKey, 4> Children(const TileKey& key);
};

/// The heights of one tile of a scheme at its level's spacing, as a piece of the unbounded terrain.
///
/// The tile equals the same cells of any larger grid on the same lattice (to the bit where cell
/// centres coincide exactly, such as with power-of-two spacings).
/// @param threads Threads to use; 0 for every hardware thread.
/// @throws std::invalid_argument for an invalid scheme, level or terrain; and whatever the parts
/// throw.
Raster<float> EvaluateTile(const Terrain& terrain, const TileScheme& scheme, const TileKey& key,
                           unsigned threads = 0);

/// `EvaluateTile` for several tiles, split over threads, in the order of `keys`.
/// @param threads Threads to use; 0 for every hardware thread.
/// @throws std::invalid_argument as `EvaluateTile`.
std::vector<Raster<float>> EvaluateTiles(const Terrain& terrain, const TileScheme& scheme,
                                         const std::vector<TileKey>& keys, unsigned threads = 0);

/// Tiles of one terrain and scheme, evaluated on demand with `EvaluateTile` and kept up to
/// a byte budget, least recently used first out. Safe to use from several threads at once; tiles
/// are re-evaluated identically, so eviction never changes results.
class TileCache {
  public:
    /// A cache of the tiles of `terrain` on `scheme`, holding up to `maxBytes` bytes.
    /// @throws std::invalid_argument for an invalid scheme or terrain, or a zero `maxBytes`.
    TileCache(Terrain terrain, TileScheme scheme, std::size_t maxBytes = std::size_t{1} << 30);

    /// A tile, evaluated on one thread when it is not held.
    /// @throws std::invalid_argument for a level outside the scheme's; and whatever the terrain's
    /// parts throw.
    std::shared_ptr<const Raster<float>> Get(const TileKey& key);

    /// Bytes held: four per height.
    std::size_t Size() const {
        return cache_.Size();
    }

    /// The byte budget given at construction.
    std::size_t MaxBytes() const {
        return cache_.Capacity();
    }

    /// Tiles evicted since construction.
    std::size_t Evictions() const {
        return cache_.Evictions();
    }

  private:
    Terrain terrain_;
    TileScheme scheme_;
    LruCache<TileKey, Raster<float>, TileKeyHash> cache_;
};

}  // namespace terraingen
