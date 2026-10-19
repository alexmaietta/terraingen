/// @file
/// @brief `MidpointDisplacementLayer`, random midpoint displacement (diamond-square) expansion of a
/// base lattice, as Martin, Parkes and Dunstan (2014) expand DEMs.
#pragma once

#include "terraingen/layers/layer.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <cstdint>
#include <vector>

namespace terraingen {

/// What a `MidpointDisplacementLayer` displaces from.
enum class MidpointBase {
    /// The ground at the base lattice's nodes, interpolated in square and diamond steps: the
    /// heights are replaced by the expanded lattice, as when expanding a DEM.
    DiamondSquare,
    /// Zero at the base lattice's nodes: only the displacements are added to the heights, keeping
    /// the source's own interpolation between nodes.
    Source,
};

/// The lattice and fractal a `MidpointDisplacementLayer` expands.
struct MidpointDisplacementOptions {
    /// Spacing of the base lattice's nodes, in surface units (metres on a plane), such as a DEM's
    /// cell size.
    double pixelSize = 1.0;
    /// A node of the base lattice in surface coordinates, such as a DEM's first cell centre.
    Vec2d origin{};
    /// Fractal dimension F in [2, 3]; each square or diamond step scales the displacement by
    /// 2^-((3 - F) / 2).
    double fractalDimension = 2.2;
    /// Standard deviation in metres of the displacement at the base lattice's own last diamond
    /// step (Se); the first square step of the expansion displaces by Se 2^-((3 - F) / 2).
    double scale = 1.0;
    /// The finest node spacing the lattice is expanded to, in surface units; 0 for no limit.
    double minPixelSize = 0.0;
    MidpointBase base = MidpointBase::DiamondSquare;
    std::uint32_t seed = 1;  ///< Varies the displacements.

    bool operator==(const MidpointDisplacementOptions& other) const {
        return pixelSize == other.pixelSize && origin.x == other.origin.x &&
               origin.y == other.origin.y && fractalDimension == other.fractalDimension &&
               scale == other.scale && minPixelSize == other.minPixelSize && base == other.base &&
               seed == other.seed;
    }
    bool operator!=(const MidpointDisplacementOptions& other) const {
        return !(*this == other);
    }
};

/// Checks midpoint displacement options.
/// @throws std::invalid_argument for a `pixelSize` that is not positive and finite, a non-finite
/// `origin`, a `fractalDimension` outside [2, 3], a negative or non-finite `scale`, or a negative
/// or non-finite `minPixelSize`.
void ValidateMidpointDisplacementOptions(const MidpointDisplacementOptions& options);

/// The displacement scale and fractal dimension a raster's heights show.
struct MidpointRoughness {
    double scale = 0.0;             ///< Se in metres.
    double fractalDimension = 2.5;  ///< F in [2, 3].
};

/// The roughness of `heights` on its own cells: the scale is the RMS of each cell minus the mean of
/// its four neighbours, what a last diamond step would have displaced it by, and the fractal
/// dimension 3 - H, H the slope of the log RMS height difference over log lag at lags of 1, 2, 4
/// and 8 cells along rows and columns (the variogram method), clamped to [0, 1]. Cells with no
/// data are skipped; a raster without relief gives a scale of 0 and dimension 2.5.
///
/// Rough fBm surfaces (F near 3) read about 0.2 low, and expansions, not being stationary fBm,
/// read up to about 0.25 off their dimension.
/// @throws std::invalid_argument for a raster with no cell whose four neighbours hold data.
MidpointRoughness EstimateMidpointRoughness(const Raster<float>& heights);

/// Node heights of a midpoint displacement lattice at one level over a window of node indices.
struct MidpointNodes {
    std::int64_t col0 = 0;  ///< First node column.
    std::int64_t row0 = 0;  ///< First node row.
    std::size_t width = 0;
    std::size_t height = 0;
    std::vector<double> values;  ///< Row by row; NaN where the ground has no data.

    double At(std::int64_t col, std::int64_t row) const {
        return values[static_cast<std::size_t>(row - row0) * width +
                      static_cast<std::size_t>(col - col0)];
    }
};

/// Random midpoint displacement of a base lattice (Martin, Parkes and Dunstan 2014; Peitgen and
/// Saupe 1988): each level halves the node spacing, setting square nodes to the mean of their four
/// corners and then diamond nodes to the mean of their four neighbours, each plus a Gaussian
/// displacement that shrinks by 2^-((3 - F) / 2) per step.
///
/// A sample at spacing s expands as many levels as halve the base spacing to at most s (up to
/// `minPixelSize`), and between nodes interpolates them bilinearly. Displacements are keyed by
/// node and level and nodes never change once made, so a grid on a lattice twice as fine holds
/// the coarser one's heights at every other node, and grids, tiles and points agree. At spacings
/// no finer than the base lattice the layer leaves the heights alone.
///
/// With `MidpointBase::DiamondSquare` the base nodes read the ground at the base spacing, so put
/// layers that add detail finer than the base lattice after this one.
class MidpointDisplacementLayer final : public Layer {
  public:
    /// @throws std::invalid_argument as `ValidateMidpointDisplacementOptions`.
    explicit MidpointDisplacementLayer(MidpointDisplacementOptions options, Surface surface = {});

    /// The levels expanded at a sample spacing of `spacing` metres.
    int Levels(double spacing) const;

    /// The node spacing at `level`, in surface units.
    double NodeSpacing(int level) const;

    /// The nodes of `level` over columns [`col0`, `col1`] and rows [`row0`, `row1`]; node (c, r)
    /// lies at `origin` + (c, r) `NodeSpacing(level)`.
    /// @param ground The terrain beneath, read at the base nodes; null with `MidpointBase::Source`.
    /// @throws std::invalid_argument for `MidpointBase::DiamondSquare` without a ground.
    MidpointNodes Nodes(int level, std::int64_t col0, std::int64_t row0, std::int64_t col1,
                        std::int64_t row1, const Ground* ground) const;

    /// The options the layer was made with.
    const MidpointDisplacementOptions& Options() const {
        return options_;
    }

  protected:
    void ApplySamples(SampleBlock& samples, const LayerContext& context) const override;

  private:
    /// The standard deviation of the displacement of the square (`diamond` false) or diamond step
    /// that makes `level`.
    double StepScale(int level, bool diamond) const;
    /// The base nodes over a window.
    MidpointNodes BaseNodes(std::int64_t col0, std::int64_t row0, std::int64_t col1,
                            std::int64_t row1, const Ground* ground) const;
    /// The expanded height at (`x`, `y`) from nodes of `level` covering it.
    double Interpolate(const MidpointNodes& nodes, int level, double x, double y) const;
    /// Applies the expansion at `level` to `heights` at `positions`.
    void Expand(const std::vector<Vec2d>& positions, std::vector<float*>& heights, int level,
                const Ground* ground) const;

    MidpointDisplacementOptions options_;
    Surface surface_;
    int maxLevels_ = 0;
    double stepFactor_ = 1.0;
};

}  // namespace terraingen
