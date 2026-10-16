/// @file
/// @brief Surface cells for per-cell feature generation: planar squares and cube-sphere cells.
#pragma once

#include "terraingen/util/geometry.h"

#include <compare>
#include <cstdint>
#include <vector>

namespace terraingen {

/// @brief Integer coordinates of one cell in a grid of square cells.
struct CellIndex {
    std::int32_t i = 0;  ///< Column, or face * n + column on a cube sphere.
    std::int32_t j = 0;  ///< Row.

    /// @brief Orders cells by `i`, then `j`.
    auto operator<=>(const CellIndex&) const = default;
};

/// @brief Partitions a surface into cells so features can be generated per cell, each from its
/// own random substream, identically however a region is tiled.
class SurfaceCells {
  public:
    virtual ~SurfaceCells() = default;

    /// @brief The cells that intersect a region, in a fixed order.
    /// @param region The region, in surface coordinates.
    /// @param cellSize The cell side in metres.
    /// @return The cells covering `region`.
    /// @throws std::invalid_argument if `cellSize` is not positive and finite or `region` is empty
    /// or not finite.
    virtual std::vector<CellIndex> CellsCovering(const Bounds2d& region, double cellSize) const = 0;

    /// @brief The area of a cell.
    /// @param cell The cell.
    /// @param cellSize The cell side in metres.
    /// @return The area in m^2.
    virtual double CellArea(const CellIndex& cell, double cellSize) const = 0;

    /// @brief Maps uniform coordinates to a point of a cell; uniform `u` and `v` give a point
    /// uniform over the cell.
    /// @param u First uniform coordinate, in [0, 1).
    /// @param v Second uniform coordinate, in [0, 1).
    /// @param cell The cell.
    /// @param cellSize The cell side in metres.
    /// @return The point in surface coordinates.
    virtual Vec2d SamplePoint(double u, double v, const CellIndex& cell, double cellSize) const = 0;
};

/// @brief Square cells on a plane: cell (i, j) of side L covers [i L, (i + 1) L) x
/// [j L, (j + 1) L).
class PlanarCells final : public SurfaceCells {
  public:
    /// @brief The cells intersecting `region`, in row-major order: increasing j, then increasing i.
    /// @param region The region, in metres.
    /// @param cellSize The cell side in metres.
    /// @return The cells covering `region`.
    /// @throws std::invalid_argument if `cellSize` is not positive and finite, `region` is empty or
    /// not finite, or a cell index would overflow 32 bits.
    std::vector<CellIndex> CellsCovering(const Bounds2d& region, double cellSize) const override;

    /// @brief The area of a cell.
    /// @param cell The cell.
    /// @param cellSize The cell side in metres.
    /// @return `cellSize` squared, in m^2.
    double CellArea(const CellIndex& cell, double cellSize) const override;

    /// @brief Maps uniform coordinates to a point of a cell.
    /// @param u Fraction across the cell in x, in [0, 1).
    /// @param v Fraction across the cell in y, in [0, 1).
    /// @param cell The cell.
    /// @param cellSize The cell side in metres.
    /// @return The point in metres.
    Vec2d SamplePoint(double u, double v, const CellIndex& cell, double cellSize) const override;
};

/// @brief Near-square, near-equal-area cells on a sphere, in longitude and latitude degrees: each
/// face of a cube split into n x n cells along equiangular coordinates.
///
/// For a cell side L, n = ceil(R pi / (2 L)). Cell (i, j) is column i mod n and row j of face
/// i / n (+x, -x, +y, -y, +z, -z).
class CubeSphereCells final : public SurfaceCells {
  public:
    /// @brief Cells on a sphere of the given radius.
    /// @param radius The sphere's radius in metres.
    /// @throws std::invalid_argument unless `radius` is positive and finite.
    explicit CubeSphereCells(double radius);

    /// @brief Every cell reaching into a region, plus some next to them, sorted by i, then j.
    /// @param region The region in longitude and latitude degrees; longitudes may run past
    /// [-180, 180].
    /// @param cellSize The cell side in metres.
    /// @return The cells covering `region`.
    /// @throws std::invalid_argument if `cellSize` is not positive and finite, the cells would be
    /// too small for the sphere, or `region` is not finite or inverted.
    std::vector<CellIndex> CellsCovering(const Bounds2d& region, double cellSize) const override;

    /// @brief The exact area of a cell, from its solid angle.
    /// @param cell The cell.
    /// @param cellSize The cell side in metres.
    /// @return The area in m^2.
    /// @throws std::invalid_argument as `CellsPerEdge`.
    double CellArea(const CellIndex& cell, double cellSize) const override;

    /// @brief Maps uniform coordinates to a point uniform over a cell.
    /// @param u First uniform coordinate, in [0, 1).
    /// @param v Second uniform coordinate, in [0, 1).
    /// @param cell The cell.
    /// @param cellSize The cell side in metres.
    /// @return Longitude and latitude in degrees, longitude in (-180, 180].
    /// @throws std::invalid_argument as `CellsPerEdge`.
    Vec2d SamplePoint(double u, double v, const CellIndex& cell, double cellSize) const override;

    /// @brief The cell holding a point.
    /// @param lonDeg Longitude in degrees.
    /// @param latDeg Latitude in degrees.
    /// @param cellSize The cell side in metres.
    /// @return The cell holding the point.
    /// @throws std::invalid_argument as `CellsPerEdge`.
    CellIndex CellOf(double lonDeg, double latDeg, double cellSize) const;

    /// @brief A cell and the cells up to `rings` steps from it along its face's rows and columns,
    /// continuing onto neighbouring faces past an edge, without repeats, sorted by i, then j.
    /// @param cell The centre cell.
    /// @param cellSize The cell side in metres.
    /// @param rings Steps out from `cell`.
    /// @return The neighbourhood, including `cell`.
    /// @throws std::invalid_argument as `CellsPerEdge`.
    std::vector<CellIndex> Neighbours(const CellIndex& cell, double cellSize, int rings) const;

    /// @brief The unit direction of a face at equiangular coordinates.
    /// @param face The face, 0 to 5: +x, -x, +y, -y, +z, -z.
    /// @param alpha Angle from the face's centre along its first axis, radians in [-pi/4, pi/4].
    /// @param beta Angle from the face's centre along its second axis, radians in [-pi/4, pi/4].
    /// @return The unit direction.
    static Vec3d Direction(int face, double alpha, double beta);

    /// @brief The number of cells along each edge of a face.
    /// @param cellSize The cell side in metres.
    /// @return n = ceil(R pi / (2 `cellSize`)).
    /// @throws std::invalid_argument if `cellSize` is not positive and finite or the cells would be
    /// too small for the sphere.
    std::int32_t CellsPerEdge(double cellSize) const;

    /// @brief The sphere's radius.
    /// @return The radius in metres.
    double Radius() const {
        return radius_;
    }

  private:
    double radius_;
};

}  // namespace terraingen
