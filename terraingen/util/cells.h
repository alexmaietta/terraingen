/// @file
/// @brief Surface cells for per-cell feature generation: planar squares and cube-sphere cells.
#pragma once

#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <array>
#include <compare>
#include <cstdint>
#include <memory>
#include <vector>

namespace terraingen {

namespace detail {
struct CubeFace;
}  // namespace detail

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
    /// @throws std::invalid_argument if `cellSize` is not positive and finite, or `region` is not
    /// finite or not one the implementation accepts.
    virtual std::vector<CellIndex> CellsCovering(const Bounds2d& region, double cellSize) const = 0;

    /// The area of a cell in m^2.
    /// @param cellSize The cell side in metres.
    virtual double CellArea(const CellIndex& cell, double cellSize) const = 0;

    /// Maps uniform `u` and `v` in [0, 1) to a point uniform over a cell, in surface coordinates;
    /// uniform over `Sampling`'s measure where that thins.
    /// @param cellSize The cell side in metres.
    virtual Vec2d SamplePoint(double u, double v, const CellIndex& cell, double cellSize) const = 0;

    /// How to place features in a cell uniformly by area: draw a Poisson count over `area`
    /// m^2, place each at `SamplePoint`, and keep it with chance `Weight(point) / bound` when
    /// `bound` is positive. Without thinning (`bound` 0) `area` is `CellArea` and every point is
    /// kept, as on a plane or a sphere.
    struct Sampling {
        double area = 0.0;   ///< Area in m^2 the Poisson count is drawn over, >= `CellArea`.
        double bound = 0.0;  ///< Upper bound of `Weight` over the cell; 0 when nothing is thinned.
    };

    /// The sampling of a cell, as `Sampling` describes.
    /// @param cellSize The cell side in metres.
    virtual Sampling CellSampling(const CellIndex& cell, double cellSize) const {
        return {CellArea(cell, cellSize), 0.0};
    }

    /// The weight of a sampled point, kept with chance `Weight(point) / Sampling::bound`.
    virtual double Weight(const Vec2d& /*point*/) const {
        return 1.0;
    }
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

/// Near-square cells on a body, in longitude and latitude degrees: each face of a cube split into
/// n x n cells along equiangular coordinates of the direction from the centre.
///
/// For a cell side L, n = ceil(R pi / (2 L)), with R the sphere's radius or an ellipsoid's
/// shortest semi-axis, so no cell is narrower than L. Cell (i, j) is column i mod n and row j of
/// face i / n (+x, -x, +y, -y, +z, -z). On a sphere cells are near-equal-area; on an ellipsoid
/// `CellArea` is each cell's true area and `CellSampling` thins points to be uniform by area.
class CubeSphereCells final : public SurfaceCells {
  public:
    /// Cells on a sphere of `radius` metres.
    /// @throws std::invalid_argument unless `radius` is positive and finite.
    explicit CubeSphereCells(double radius);

    /// Cells on a body, a sphere or an ellipsoid.
    /// @throws std::invalid_argument for a plane.
    explicit CubeSphereCells(const Surface& surface);

    /// Every cell reaching into a region, plus some next to them, sorted by i, then j.
    /// @param region The region in longitude and latitude degrees; longitudes may run past
    /// [-180, 180].
    /// @param cellSize The cell side in metres.
    /// @throws std::invalid_argument if `cellSize` is not positive and finite, the cells would be
    /// too small for the sphere, or `region` is not finite or inverted; a point is allowed.
    std::vector<CellIndex> CellsCovering(const Bounds2d& region, double cellSize) const override;

    /// The area of a cell in m^2: exact from its solid angle on a sphere, by quadrature of the area
    /// element on an ellipsoid.
    /// @throws std::invalid_argument as `CellsPerEdge`.
    double CellArea(const CellIndex& cell, double cellSize) const override;

    /// `CellArea` without thinning on a sphere; on an ellipsoid, the cell's solid angle times a
    /// guaranteed bound of the area per steradian over it (from interval bounds of its directions,
    /// capped by the bound over the whole body), thinned by `Weight`.
    /// @throws std::invalid_argument as `CellsPerEdge`.
    Sampling CellSampling(const CellIndex& cell, double cellSize) const override;

    /// The area per steradian at a point (`Surface::AreaPerSteradian`).
    double Weight(const Vec2d& point) const override;

    /// Maps uniform `u` and `v` in [0, 1) to a point uniform in solid angle over a cell, as
    /// longitude in (-180, 180] and latitude, in degrees.
    /// @throws std::invalid_argument as `CellsPerEdge`.
    Vec2d SamplePoint(double u, double v, const CellIndex& cell, double cellSize) const override;

    /// The cell holding a point given in degrees.
    /// @param cellSize The cell side in metres.
    /// @throws std::invalid_argument as `CellsPerEdge`.
    CellIndex CellAt(double lonDeg, double latDeg, double cellSize) const;

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

    /// The radius in metres the cells are sized by: the sphere's, or an ellipsoid's shortest.
    double Radius() const {
        return sphere_.Radius();
    }

  private:
    /// The gnomonic bounds a1, a2, b1, b2 of a cell on its face of `n` cells per edge.
    std::array<double, 4> Bounds(const CellIndex& cell, std::int32_t n) const;

    /// An upper bound of `Weight` over the gnomonic rectangle a1, a2, b1, b2 of `face`, from
    /// interval bounds of the direction's squared components.
    double WeightBound(std::size_t face, const std::array<double, 4>& bounds) const;

    /// Adds the cells holding sample points of cell (`col`, `row`) of `face`, which lies past the
    /// face's edge, so on its neighbours.
    void AddAcrossEdge(const detail::CubeFace& face, std::int32_t col, std::int32_t row,
                       std::int32_t n, double cellSize, std::vector<CellIndex>& cells) const;

    /// The sorted cells holding a lattice of points over `box`, finer than the cells.
    std::vector<CellIndex> Seeds(const Bounds2d& box, double cellSize) const;

    /// The `seeds` plus the ring `cells` around them that may reach `box`.
    std::vector<CellIndex> KeepReaching(const Bounds2d& box, const std::vector<CellIndex>& seeds,
                                        const std::vector<CellIndex>& cells, double cellSize) const;

    Surface surface_;
    Surface sphere_;          ///< The sphere of `Radius`, which cell geometry is computed on.
    double weightCap_ = 0.0;  ///< Bound of `Weight` over the whole ellipsoid; 0 on a sphere.
};

/// The cells features are generated in on `surface`: `PlanarCells` on a plane, `CubeSphereCells`
/// on a body.
std::shared_ptr<const SurfaceCells> MakeCells(const Surface& surface);

}  // namespace terraingen
