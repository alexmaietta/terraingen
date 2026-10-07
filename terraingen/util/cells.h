#pragma once

#include "terraingen/util/geometry.h"

#include <cstdint>
#include <vector>

namespace terraingen {

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

    /// The point of `cell` at uniform coordinates `u` and `v` in [0, 1), in surface coordinates;
    /// uniformly distributed `u` and `v` give a point uniformly distributed over the cell.
    virtual Vec2d SamplePoint(double u, double v, const CellIndex& cell, double cellSize) const = 0;
};

/// Square cells on a plane: cell (i, j) of side L covers [i L, (i + 1) L) x [j L, (j + 1) L).
class PlanarCells final : public SurfaceCells {
  public:
    /// Cells in row-major order: increasing j, then increasing i.
    std::vector<CellIndex> CellsCovering(const Bounds2d& region, double cellSize) const override;

    double CellArea(const CellIndex& cell, double cellSize) const override;

    Vec2d SamplePoint(double u, double v, const CellIndex& cell, double cellSize) const override;
};

/// Cells on a sphere of radius `radius` metres, in longitude and latitude degrees: the six faces
/// of a cube, each split into n x n cells along equiangular coordinates (Ronchi et al. 1996), the
/// angle from the face's centre along each axis, so cells are near squares of about the same area.
///
/// For a cell side L, n = ceil(R pi / (2 L)), so cells are at most L across at a face's centre and
/// a little smaller towards its corners. Cell (i, j) is column i mod n and row j of face i / n
/// (+x, -x, +y, -y, +z, -z). Areas are the exact solid angles of the cells, and `SamplePoint`
/// maps uniform (u, v) to points uniform over the cell, so features are drawn with the right
/// density everywhere.
///
/// Reference: C. Ronchi, R. Iacono and P. S. Paolucci, "The 'Cubed Sphere': a new method for the
/// solution of partial differential equations in spherical geometry", Journal of Computational
/// Physics 124:93-114, 1996. https://doi.org/10.1006/jcph.1996.0047
class CubeSphereCells final : public SurfaceCells {
  public:
    /// @throws std::invalid_argument unless `radius` is positive and finite.
    explicit CubeSphereCells(double radius);

    /// Every cell with a point within its own size of `region` (longitudes may run past
    /// [-180, 180]), found by sampling the region widened by a cell on a lattice an eighth of a
    /// cell apart; sorted by i, then j. A cell reaching into `region` is always included.
    std::vector<CellIndex> CellsCovering(const Bounds2d& region, double cellSize) const override;

    double CellArea(const CellIndex& cell, double cellSize) const override;

    /// A point of `cell` as longitude and latitude in degrees, longitude in (-180, 180].
    Vec2d SamplePoint(double u, double v, const CellIndex& cell, double cellSize) const override;

    /// The cell holding the point at longitude `lonDeg` and latitude `latDeg`.
    CellIndex CellOf(double lonDeg, double latDeg, double cellSize) const;

    /// Cells along each edge of a face for cells `cellSize` metres across.
    std::int32_t CellsPerEdge(double cellSize) const;

    double Radius() const {
        return radius_;
    }

  private:
    double radius_;
};

}  // namespace terraingen
