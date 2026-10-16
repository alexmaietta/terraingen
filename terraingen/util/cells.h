/// @file
/// @brief Surface cells for per-cell feature generation: planar squares and cube-sphere cells.
#pragma once

#include "terraingen/util/geometry.h"

#include <compare>
#include <cstdint>
#include <vector>

namespace terraingen {

/// Integer coordinates of one cell in a grid of square cells.
struct CellIndex {
    std::int32_t i = 0;  ///< Column, or face * n + column on a cube sphere.
    std::int32_t j = 0;  ///< Row.

    /// Orders cells by `i`, then `j`.
    auto operator<=>(const CellIndex&) const = default;
};

/// Partitions a surface into cells so features can be generated per cell, each from its
/// own random substream, identically however a region is tiled.
class SurfaceCells {
  public:
    virtual ~SurfaceCells() = default;

    /// The cells that intersect `region` (surface coordinates), in a fixed order.
    /// @param cellSize The cell side in metres.
    /// @throws std::invalid_argument if `cellSize` is not positive and finite or `region` is empty
    /// or not finite.
    virtual std::vector<CellIndex> CellsCovering(const Bounds2d& region, double cellSize) const = 0;

    /// The area of a cell in m^2.
    /// @param cellSize The cell side in metres.
    virtual double CellArea(const CellIndex& cell, double cellSize) const = 0;

    /// Maps uniform `u` and `v` in [0, 1) to a point uniform over a cell, in surface coordinates.
    /// @param cellSize The cell side in metres.
    virtual Vec2d SamplePoint(double u, double v, const CellIndex& cell, double cellSize) const = 0;
};

/// Square cells on a plane: cell (i, j) of side L covers [i L, (i + 1) L) x
/// [j L, (j + 1) L).
class PlanarCells final : public SurfaceCells {
  public:
    /// The cells intersecting `region` (metres), in row-major order: increasing j, then i.
    /// @param cellSize The cell side in metres.
    /// @throws std::invalid_argument if `cellSize` is not positive and finite, `region` is empty or
    /// not finite, or a cell index would overflow 32 bits.
    std::vector<CellIndex> CellsCovering(const Bounds2d& region, double cellSize) const override;

    /// `cellSize` squared, in m^2.
    double CellArea(const CellIndex& cell, double cellSize) const override;

    /// The point at fractions `u` and `v` in [0, 1) across a cell in x and y, in metres.
    Vec2d SamplePoint(double u, double v, const CellIndex& cell, double cellSize) const override;
};

/// Near-square, near-equal-area cells on a sphere, in longitude and latitude degrees: each
/// face of a cube split into n x n cells along equiangular coordinates.
///
/// For a cell side L, n = ceil(R pi / (2 L)). Cell (i, j) is column i mod n and row j of face
/// i / n (+x, -x, +y, -y, +z, -z).
class CubeSphereCells final : public SurfaceCells {
  public:
    /// Cells on a sphere of `radius` metres.
    /// @throws std::invalid_argument unless `radius` is positive and finite.
    explicit CubeSphereCells(double radius);

    /// Every cell reaching into a region, plus some next to them, sorted by i, then j.
    /// @param region The region in longitude and latitude degrees; longitudes may run past
    /// [-180, 180].
    /// @param cellSize The cell side in metres.
    /// @throws std::invalid_argument if `cellSize` is not positive and finite, the cells would be
    /// too small for the sphere, or `region` is not finite or inverted.
    std::vector<CellIndex> CellsCovering(const Bounds2d& region, double cellSize) const override;

    /// The exact area of a cell in m^2, from its solid angle.
    /// @throws std::invalid_argument as `CellsPerEdge`.
    double CellArea(const CellIndex& cell, double cellSize) const override;

    /// Maps uniform `u` and `v` in [0, 1) to a point uniform over a cell, as longitude in
    /// (-180, 180] and latitude, in degrees.
    /// @throws std::invalid_argument as `CellsPerEdge`.
    Vec2d SamplePoint(double u, double v, const CellIndex& cell, double cellSize) const override;

    /// The cell holding a point given in degrees.
    /// @param cellSize The cell side in metres.
    /// @throws std::invalid_argument as `CellsPerEdge`.
    CellIndex CellOf(double lonDeg, double latDeg, double cellSize) const;

    /// A cell and the cells up to `rings` steps from it along its face's rows and columns,
    /// continuing onto neighbouring faces past an edge, without repeats, sorted by i, then j.
    /// @param cellSize The cell side in metres.
    /// @throws std::invalid_argument as `CellsPerEdge`.
    std::vector<CellIndex> Neighbours(const CellIndex& cell, double cellSize, int rings) const;

    /// The unit direction of a face at equiangular coordinates.
    /// @param face The face, 0 to 5: +x, -x, +y, -y, +z, -z.
    /// @param alpha Angle from the face's centre along its first axis, radians in [-pi/4, pi/4].
    /// @param beta Angle from the face's centre along its second axis, radians in [-pi/4, pi/4].
    static Vec3d Direction(int face, double alpha, double beta);

    /// The number of cells along each edge of a face, n = ceil(R pi / (2 `cellSize`)).
    /// @param cellSize The cell side in metres.
    /// @throws std::invalid_argument if `cellSize` is not positive and finite or the cells would be
    /// too small for the sphere.
    std::int32_t CellsPerEdge(double cellSize) const;

    /// The sphere's radius in metres.
    double Radius() const {
        return radius_;
    }

  private:
    double radius_;
};

}  // namespace terraingen
