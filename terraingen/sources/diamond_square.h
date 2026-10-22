/// @file
/// @brief `DiamondSquareUpsampler`, random midpoint displacement (diamond-square) refinement of a
/// source's posts, as Martin, Parkes and Dunstan (2014) expand DEMs.
#pragma once

#include "terraingen/raster/raster.h"
#include "terraingen/sources/upsampling.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace terraingen {

/// How diamond-square fills new nodes and reads samples between the finest nodes.
enum class DiamondSquareInterp {
    /// Square nodes the mean of their four corners, diamond nodes the mean of their four
    /// neighbors, samples bilinear between nodes.
    Bilinear,
    /// Catmull-Rom along each axis over four nodes, as the VIPER terrain expansion does: square
    /// nodes from the 4 x 4 coarser nodes around them, diamond nodes the mean of the splines
    /// along the two axes, samples Catmull-Rom between nodes. Smoother, with fewer lattice
    /// creases.
    CatmullRom,
};

/// The fractal a `DiamondSquareUpsampler` refines posts with.
struct DiamondSquareOptions {
    /// Fractal dimension F in [2, 3]; each square or diamond step scales the displacement by
    /// 2^-((3 - F) / 2). Unset estimates it from the posts (`EstimateMidpointRoughness`).
    std::optional<double> fractalDimension{};
    /// Standard deviation in meters of the displacement at the posts' own last diamond step (Se);
    /// the first square step of the refinement displaces by Se 2^-((3 - F) / 2). Unset estimates
    /// it from the posts.
    std::optional<double> scale{};
    DiamondSquareInterp interp = DiamondSquareInterp::Bilinear;
    /// The finest node spacing the posts are refined to, in the posts' units (meters on a plane);
    /// 0 for no limit.
    double minPixelSize = 0.0;
    std::uint32_t seed = 1;  ///< Varies the displacements.

    /// Memberwise equality.
    bool operator==(const DiamondSquareOptions& other) const {
        return fractalDimension == other.fractalDimension && scale == other.scale &&
               interp == other.interp && minPixelSize == other.minPixelSize && seed == other.seed;
    }
    bool operator!=(const DiamondSquareOptions& other) const {
        return !(*this == other);
    }
};

/// Checks diamond-square options.
/// @throws std::invalid_argument for a `fractalDimension` outside [2, 3], a negative or
/// non-finite `scale`, or a negative or non-finite `minPixelSize`.
void ValidateDiamondSquareOptions(const DiamondSquareOptions& options);

/// The displacement scale and fractal dimension a raster's heights show.
struct MidpointRoughness {
    double scale = 0.0;             ///< Se in meters.
    double fractalDimension = 2.5;  ///< F in [2, 3].
};

/// The roughness of `heights` on its own cells: the scale is the RMS of each cell minus the mean of
/// its four neighbors, what a last diamond step would have displaced it by, and the fractal
/// dimension 3 - H, H the slope of the log RMS height difference over log lag at lags of 1, 2, 4
/// and 8 cells along rows and columns (the variogram method), clamped to [0, 1]. Cells with no
/// data are skipped; a raster without relief gives a scale of 0 and dimension 2.5.
///
/// Rough fBm surfaces (F near 3) read about 0.2 low, and expansions, not being stationary fBm,
/// read up to about 0.25 off their dimension.
/// @throws std::invalid_argument for a raster with no cell whose four neighbors hold data.
MidpointRoughness EstimateMidpointRoughness(const Raster<float>& heights);

/// Node heights of a diamond-square lattice at one level over a window of node indices.
struct MidpointNodes {
    std::int64_t col0 = 0;  ///< First node column.
    std::int64_t row0 = 0;  ///< First node row.
    std::size_t width = 0;
    std::size_t height = 0;
    std::vector<double> values;  ///< Row by row; NaN where the posts have no data.

    double At(std::int64_t col, std::int64_t row) const {
        return values[static_cast<std::size_t>(row - row0) * width +
                      static_cast<std::size_t>(col - col0)];
    }
};

/// Random midpoint displacement of a source's posts (Martin, Parkes and Dunstan 2014; Peitgen and
/// Saupe 1988): each level halves the node spacing, setting square nodes from their four corners
/// and then diamond nodes from their four neighbors (`DiamondSquareInterp`), each plus a Gaussian
/// displacement that shrinks by 2^-((3 - F) / 2) per step.
///
/// The level-0 nodes are the posts: node (c, r) lies at post (0, 0) plus c posts along the
/// lattice's columns and r posts against its rows, north for a north-up lattice, and holds post
/// (c, -r). A sample at spacing s refines ceil(log2(post spacing / s)) levels, up to
/// `minPixelSize`, and reads the nodes of that level between them. Displacements are keyed by
/// node and level and nodes never change once made, so a grid on a lattice twice as fine holds
/// the coarser one's heights at every other node, and grids, tiles and points agree. At spacings
/// no finer than the posts it interpolates the posts.
class DiamondSquareUpsampler final : public Upsampler {
  public:
    /// @throws std::invalid_argument as `ValidateDiamondSquareOptions`.
    explicit DiamondSquareUpsampler(DiamondSquareOptions options = {});

    /// The levels refined at a sample spacing of `spacing` over posts `postSpacing` apart, both in
    /// the posts' units.
    int Levels(double postSpacing, double spacing) const;

    /// The nodes of `level` over columns [`col0`, `col1`] and rows [`row0`, `row1`] of the
    /// lattice of `posts` refined `level` times.
    /// @throws std::invalid_argument without a fractal dimension and scale (`FitTo`), or for a
    /// rotated lattice.
    MidpointNodes Nodes(const PostLattice& posts, int level, std::int64_t col0, std::int64_t row0,
                        std::int64_t col1, std::int64_t row1) const;

    /// @throws std::invalid_argument as `Nodes`.
    float HeightAt(const PostLattice& posts, double x, double y, double spacing) const override;

    /// Refines the nodes once per block of cells, blocks over every hardware thread.
    /// @throws std::invalid_argument as `Nodes`.
    void Fill(const PostLattice& posts, Raster<float>& heights, double spacing) const override;

    /// Posts either side of a sample its nodes are refined from.
    int Reach() const override;

    /// A copy with an unset fractal dimension and scale estimated from up to 512 x 512 of the posts
    /// held (`EstimateMidpointRoughness`), or 256 x 256 posts from post (0, 0) for a lattice
    /// without bounds; null when both are set.
    /// @throws std::invalid_argument as `EstimateMidpointRoughness`.
    std::shared_ptr<const Upsampler> FitTo(const PostLattice& posts) const override;

    /// The options the upsampler was made with.
    const DiamondSquareOptions& Options() const {
        return options_;
    }

  private:
    /// Where the nodes lie: post (0, 0) and the node steps of level 0 along x and y.
    struct NodeFrame {
        double originX = 0.0;
        double originY = 0.0;
        double stepX = 1.0;
        double stepY = 1.0;
    };

    /// The node frame of `posts`.
    /// @throws std::invalid_argument for a rotated lattice.
    static NodeFrame Frame(const PostLattice& posts);
    /// The standard deviation of the displacement of the square (`diamond` false) or diamond step
    /// that makes `level`.
    double StepScale(int level, bool diamond) const;
    /// The posts as nodes of level 0 over a window.
    static MidpointNodes BaseNodes(const PostLattice& posts, std::int64_t col0, std::int64_t row0,
                                   std::int64_t col1, std::int64_t row1);
    /// The window of node indices at `level` that the samples at `positions` read.
    void Window(const NodeFrame& frame, int level, const std::vector<Vec2d>& positions,
                std::int64_t& col0, std::int64_t& row0, std::int64_t& col1,
                std::int64_t& row1) const;
    /// The height at (`x`, `y`) from nodes of `level` covering it.
    double Interpolate(const NodeFrame& frame, const MidpointNodes& nodes, int level, double x,
                       double y) const;
    /// The heights at `positions` into `heights`, from one window of nodes.
    void Expand(const PostLattice& posts, const std::vector<Vec2d>& positions,
                std::vector<float>& heights, double spacing) const;

    DiamondSquareOptions options_;
    double stepFactor_ = 1.0;
};

}  // namespace terraingen
