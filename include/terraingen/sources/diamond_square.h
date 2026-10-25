// SPDX-License-Identifier: BSD-3-Clause

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

/// The largest `DiamondSquareOptions::fitWindow`, in posts.
inline constexpr std::size_t kMaxDiamondSquareFitWindow = 1024;

/// The fractal a `DiamondSquareUpsampler` refines posts with.
struct DiamondSquareOptions {
    /// Fractal dimension F in [2, 3]; each square or diamond step scales the displacement by
    /// 2^-((3 - F) / 2). Unset estimates it from the posts (`EstimateMidpointRoughness`).
    std::optional<double> fractalDimension{};
    /// Standard deviation in meters of the displacement at the posts' own last diamond step (Se);
    /// the first square step of the refinement displaces by Se 2^-((3 - F) / 2). Unset estimates
    /// it from the posts, window by window (`fitWindow`).
    std::optional<double> scale{};
    /// Side in posts of the overlapping windows an unset `scale` is fitted over, centered every
    /// half side from post (0, 0), so each region's detail follows its own relief and a crater or
    /// massif roughens only the windows that hold it; each node's scale is bilinear between the
    /// fits of the windows around it (`EstimateMidpointScale`). 0 fits one scale to the posts
    /// `FitTo` reads; else 4 to `kMaxDiamondSquareFitWindow`.
    std::size_t fitWindow = 32;
    DiamondSquareInterp interp = DiamondSquareInterp::Bilinear;
    /// The finest node spacing the posts are refined to, in the posts' units (meters on a plane);
    /// 0 for no limit.
    double minPixelSize = 0.0;
    std::uint32_t seed = 1;  ///< Varies the displacements.

    /// Memberwise equality.
    bool operator==(const DiamondSquareOptions& other) const {
        return fractalDimension == other.fractalDimension && scale == other.scale &&
               fitWindow == other.fitWindow && interp == other.interp &&
               minPixelSize == other.minPixelSize && seed == other.seed;
    }
    bool operator!=(const DiamondSquareOptions& other) const {
        return !(*this == other);
    }
};

/// Checks diamond-square options.
/// @throws std::invalid_argument for a `fractalDimension` outside [2, 3], a negative or
/// non-finite `scale`, a `fitWindow` of 1 to 3 or above `kMaxDiamondSquareFitWindow`, or a
/// negative or non-finite `minPixelSize`.
void ValidateDiamondSquareOptions(const DiamondSquareOptions& options);

/// The displacement scale and fractal dimension a raster's heights show.
struct MidpointRoughness {
    double scale = 0.0;             ///< Se in meters.
    double fractalDimension = 2.5;  ///< F in [2, 3].
};

/// The displacement scale Se of `heights` on its own cells, robust to a few large features: the
/// median absolute difference of each cell and the mean of its four neighbors, times 1.4826 so
/// that Gaussian relief gives its RMS; 0 without such a cell.
double EstimateMidpointScale(const Raster<float>& heights);

/// The roughness of `heights` on its own cells, by medians so that a few large features such as
/// craters do not set it: the scale is `EstimateMidpointScale`, and the fractal dimension 3 - H
/// from the median absolute height differences at lags of 1, 2, 4 and 8 cells; a raster without
/// relief gives a scale of 0 and dimension 2.5.
/// @throws std::invalid_argument for a raster with no cell whose four neighbors hold data.
MidpointRoughness EstimateMidpointRoughness(const Raster<float>& heights);

namespace detail {

/// Node heights of a diamond-square lattice at one level over a box of node indices; NaN where
/// the posts have no data.
using MidpointNodes = LatticeWindow<double>;

/// The diamond-square refinement of posts that a `DiamondSquareUpsampler` reads its samples from.
class MidpointRefiner {
  public:
    /// @param options Options with a fractal dimension, and a scale or a fit window, or `Nodes`
    /// throws.
    explicit MidpointRefiner(const DiamondSquareOptions& options);

    /// The levels refined at a sample spacing of `spacing` over posts `postSpacing` apart, both in
    /// the posts' units.
    int Levels(double postSpacing, double spacing) const;

    /// The nodes of `level` over `box` of the lattice of `posts` refined `level` times.
    /// @throws std::invalid_argument without a fractal dimension, or without a scale and a fit
    /// window, or for a rotated lattice.
    MidpointNodes Nodes(const PostLattice& posts, int level, const IndexBox& box) const;

    /// The scale Se at node (`col`, `row`) of `level`: the given scale, or bilinear between the
    /// fits of the windows of posts around it (`DiamondSquareOptions::fitWindow`).
    double NodeScale(const PostLattice& posts, std::int64_t col, std::int64_t row, int level) const;

    /// The heights at `positions` at a sample spacing of `spacing`, from one box of nodes.
    /// @throws std::invalid_argument as `Nodes`.
    std::vector<float> Expand(const PostLattice& posts, const std::vector<Vec2d>& positions,
                              double spacing) const;

    /// The heights at the cell centers of `heights`, tile by tile, tiles split over threads.
    /// @param threads Threads to fill over; 0 for every hardware thread.
    /// @throws std::invalid_argument as `Nodes`.
    void Fill(const PostLattice& posts, Raster<float>& heights, double spacing,
              unsigned threads) const;

  private:
    /// Where the nodes lie: post (0, 0) and the node steps of level 0 along x and y.
    struct NodeFrame {
        double originX = 0.0;
        double originY = 0.0;
        double stepX = 1.0;
        double stepY = 1.0;
    };

    /// The scales Se over the nodes being refined: one given value, or the fit of each window of
    /// posts, by window center (`DiamondSquareOptions::fitWindow`).
    struct ScaleField {
        double uniform = 0.0;
        std::int64_t step = 0;  ///< Posts between window centers; 0 for a uniform scale.
        std::optional<LatticeWindow<double>> windows;
    };

    /// The node frame of `posts`.
    /// @throws std::invalid_argument for a rotated lattice.
    static NodeFrame Frame(const PostLattice& posts);
    /// The posts as nodes of level 0 over `box`.
    static MidpointNodes BaseNodes(const PostLattice& posts, const IndexBox& box);
    /// The scales of every node that refining `box` of `level` displaces, each window's the
    /// `EstimateMidpointScale` of the posts in it, read as every read is, so one reaching past a
    /// bounded lattice reads its edge posts repeated.
    ScaleField Scales(const PostLattice& posts, int level, const IndexBox& box) const;
    /// The scale of node (`col`, `row`) of `level` from `scales`.
    static double ScaleAt(const ScaleField& scales, std::int64_t col, std::int64_t row, int level);
    /// The nodes of `level` over `box`, displaced by `scales`.
    MidpointNodes Refine(const PostLattice& posts, int level, const IndexBox& box,
                         const ScaleField& scales) const;
    /// The Gaussian displacement of node (`col`, `row`) made by the square (`diamond` false) or
    /// diamond step of `level`, for a scale Se of `scale`.
    double Displacement(std::int64_t col, std::int64_t row, int level, bool diamond,
                        double scale) const;
    /// The kept coarser nodes and the displaced square-step nodes of `level` over `box`; diamond
    /// nodes left 0.
    MidpointNodes SquareStep(const MidpointNodes& parents, int level, const IndexBox& box,
                             const ScaleField& scales) const;
    /// The nodes of `level` over `box`, adding the displaced diamond-step nodes to `squares`.
    MidpointNodes DiamondStep(const MidpointNodes& squares, int level, const IndexBox& box,
                              const ScaleField& scales) const;
    /// The box of nodes of `level` that the samples at `positions` read.
    IndexBox ReadBox(const NodeFrame& frame, int level, const std::vector<Vec2d>& positions) const;
    /// The height at (`x`, `y`) from nodes of `level` covering it.
    double Interpolate(const NodeFrame& frame, const MidpointNodes& nodes, int level, double x,
                       double y) const;

    DiamondSquareOptions options_;
    double stepFactor_ = 1.0;
};

}  // namespace detail

/// Random midpoint displacement of a source's posts (Martin, Parkes and Dunstan 2014; Peitgen and
/// Saupe 1988): each level halves the node spacing and displaces new nodes by a Gaussian that
/// shrinks by 2^-((3 - F) / 2) per step, keyed by node and level so grids, tiles and points agree.
/// Without a given scale each region's displacements follow the roughness of the posts around it.
class DiamondSquareUpsampler final : public Upsampler {
  public:
    /// @throws std::invalid_argument as `ValidateDiamondSquareOptions`.
    explicit DiamondSquareUpsampler(DiamondSquareOptions options = {});

    /// @throws std::invalid_argument without a fractal dimension (`FitTo`), or without a scale
    /// and a fit window, or for a rotated lattice.
    float HeightAt(const PostLattice& posts, double x, double y, double spacing) const override;

    using Upsampler::Fill;

    /// Refines the nodes once per tile of cells, tiles split over threads.
    /// @throws std::invalid_argument as `HeightAt`.
    void Fill(const PostLattice& posts, Raster<float>& heights, double spacing,
              unsigned threads) const override;

    /// Posts either side of a sample its nodes are refined from.
    int Reach() const override;

    /// A copy with an unset fractal dimension, and an unset scale when `fitWindow` is 0, estimated
    /// from up to 512 x 512 of the posts held (`EstimateMidpointRoughness`), or 256 x 256 posts
    /// from post (0, 0) for a lattice without bounds; null when nothing is left to estimate, as
    /// a scale fitted window by window is read from the posts as they are refined.
    /// @throws std::invalid_argument as `EstimateMidpointRoughness`.
    std::shared_ptr<const Upsampler> FitTo(const PostLattice& posts) const override;

    /// The options the upsampler was made with.
    const DiamondSquareOptions& Options() const {
        return options_;
    }

  private:
    DiamondSquareOptions options_;
    detail::MidpointRefiner refiner_;
};

}  // namespace terraingen
