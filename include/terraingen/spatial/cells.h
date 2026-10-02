#pragma once

#include "terraingen/random/random_stream.h"
#include "terraingen/raster/geo_transform.h"

#include <cstdint>
#include <vector>

namespace terraingen {

/// An axis-aligned box [`minX`, `maxX`) x [`minY`, `maxY`) in surface coordinates: metres in the
/// projected frame of a planar surface, the same world coordinates a raster's `GeoTransform` maps
/// pixels to.
struct Bounds2d {
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;

    bool Contains(double x, double y) const {
        return x >= minX && x < maxX && y >= minY && y < maxY;
    }

    /// The box grown by `margin` on every side.
    Bounds2d Expanded(double margin) const {
        return {minX - margin, minY - margin, maxX + margin, maxY + margin};
    }
};

/// Integer coordinates of one cell in a grid of square cells.
struct CellIndex {
    std::int32_t i = 0;
    std::int32_t j = 0;

    bool operator==(const CellIndex&) const = default;
};

/// Partitions a surface into cells so features can be generated per cell, each from its own random
/// substream, and any region regenerated identically regardless of how it is tiled.
///
/// Each surface type supplies its own cells: square cells on a plane now, near-equal-area cells on
/// cube-sphere faces or shape models later. Feature generators use only this interface, so they do
/// not depend on the surface's geometry.
class SurfaceCells {
  public:
    virtual ~SurfaceCells() = default;

    /// Cells of side `cellSize` metres that intersect `region`, in a fixed order.
    /// @throws std::invalid_argument if `cellSize` is not positive and finite or `region` is empty
    /// or not finite.
    virtual std::vector<CellIndex> CellsCovering(const Bounds2d& region, double cellSize) const = 0;

    /// Area of `cell`, in m^2.
    virtual double CellArea(const CellIndex& cell, double cellSize) const = 0;

    /// A point uniformly distributed over `cell`, in surface coordinates.
    virtual Vec2d SamplePoint(RandomStream& random, const CellIndex& cell,
                              double cellSize) const = 0;
};

/// Square cells on a plane: cell (i, j) of side L covers [i L, (i + 1) L) x [j L, (j + 1) L).
class PlanarCells final : public SurfaceCells {
  public:
    /// Cells in row-major order: increasing j, then increasing i.
    std::vector<CellIndex> CellsCovering(const Bounds2d& region, double cellSize) const override;

    double CellArea(const CellIndex& cell, double cellSize) const override;

    Vec2d SamplePoint(RandomStream& random, const CellIndex& cell, double cellSize) const override;
};

}  // namespace terraingen
