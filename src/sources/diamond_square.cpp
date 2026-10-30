// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/diamond_square.h"

#include "terraingen/analysis/variogram.h"
#include "terraingen/random/samplers.h"
#include "terraingen/random/stream.h"
#include "terraingen/random/streams.h"
#include "util/checks.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// Levels the posts are refined to at most.
constexpr int kMaxLevels = 30;

/// Most node reads per point at which the points of a batch are refined over one box of nodes.
constexpr double kNodesPerPoint = 64.0;

/// How close to a node, in node spacings, a sample takes the node's height.
constexpr double kOnNode = 1e-7;

/// Largest side, in posts, of the window the roughness is estimated from.
constexpr std::size_t kEstimateSide = 512;

/// Side, in posts, of the window the roughness of a lattice without bounds is estimated from.
constexpr std::size_t kUnboundedEstimateSide = 256;

/// The ratio of the standard deviation to the median absolute value of a Gaussian, 1 / Phi^-1(3/4).
constexpr double kGaussianMadScale = 1.482602218505602;

/// Posts beyond a box of nodes, at its own level, that refining it displaces nodes over.
constexpr double kScaleMargin = 10.0;

/// The longest lag, in cells, the fractal dimension is fitted over.
constexpr std::size_t kMaxRoughnessLag = 8;

/// Catmull-Rom weights of four nodes around a midpoint.
constexpr std::array<double, 4> kMidpointWeights{-1.0 / 16.0, 9.0 / 16.0, 9.0 / 16.0, -1.0 / 16.0};

std::int64_t FloorHalf(std::int64_t value) {
    return value >= 0 ? value / 2 : -((1 - value) / 2);
}

std::int64_t CeilHalf(std::int64_t value) {
    return -FloorHalf(-value);
}

bool IsOdd(std::int64_t value) {
    return (value & 1) != 0;
}

/// The node at or below `u` and the share of the way to the next, 0 within `kOnNode` of a node.
std::pair<std::int64_t, double> NodeAndShare(double u) {
    const double nearest = std::round(u);
    if (std::fabs(u - nearest) < kOnNode) {
        return {static_cast<std::int64_t>(nearest), 0.0};
    }
    const double below = std::floor(u);
    return {static_cast<std::int64_t>(below), u - below};
}

/// The box over columns [`col0`, `col1`] and rows [`row0`, `row1`].
detail::IndexBox Box(std::int64_t col0, std::int64_t row0, std::int64_t col1, std::int64_t row1) {
    detail::IndexBox box;
    box.Include(col0, col1, row0, row1);
    return box;
}

/// The square-step mean of the coarser nodes around the square whose lower-left parent is
/// (`left`, `bottom`): its four corners, or Catmull-Rom over the 4 x 4 around it.
double SquareMean(const detail::LatticeWindow<double>& parents, std::int64_t left,
                  std::int64_t bottom, bool spline) {
    if (!spline) {
        return 0.25 * (parents.At(left, bottom) + parents.At(left + 1, bottom) +
                       parents.At(left, bottom + 1) + parents.At(left + 1, bottom + 1));
    }
    double sum = 0.0;
    for (std::int64_t j = 0; j < 4; ++j) {
        double line = 0.0;
        for (std::int64_t i = 0; i < 4; ++i) {
            line += kMidpointWeights[static_cast<std::size_t>(i)] *
                    parents.At(left - 1 + i, bottom - 1 + j);
        }
        sum += kMidpointWeights[static_cast<std::size_t>(j)] * line;
    }
    return sum;
}

/// The diamond-step mean around node (`col`, `row`): its four neighbors, or the mean of the
/// Catmull-Rom splines along the two axes.
double DiamondMean(const detail::LatticeWindow<double>& squares, std::int64_t col, std::int64_t row,
                   bool spline) {
    if (!spline) {
        return 0.25 * (squares.At(col - 1, row) + squares.At(col + 1, row) +
                       squares.At(col, row - 1) + squares.At(col, row + 1));
    }
    double alongX = 0.0;
    double alongY = 0.0;
    for (std::int64_t k = 0; k < 4; ++k) {
        const double weight = kMidpointWeights[static_cast<std::size_t>(k)];
        alongX += weight * squares.At(col - 3 + 2 * k, row);
        alongY += weight * squares.At(col, row - 3 + 2 * k);
    }
    return 0.5 * (alongX + alongY);
}

/// The absolute difference of each cell and the mean of its four neighbors one cell away, over
/// the cells where all five hold data.
std::vector<double> AbsoluteResiduals(const Raster<float>& heights) {
    std::vector<double> residuals;
    const auto missing = [&](std::size_t col, std::size_t row) {
        return heights.IsMissing(heights(col, row));
    };
    for (std::size_t row = 1; row + 1 < heights.Height(); ++row) {
        for (std::size_t col = 1; col + 1 < heights.Width(); ++col) {
            if (missing(col, row) || missing(col - 1, row) || missing(col + 1, row) ||
                missing(col, row - 1) || missing(col, row + 1)) {
                continue;
            }
            const double mean = 0.25 * (static_cast<double>(heights(col - 1, row)) +
                                        static_cast<double>(heights(col + 1, row)) +
                                        static_cast<double>(heights(col, row - 1)) +
                                        static_cast<double>(heights(col, row + 1)));
            residuals.push_back(std::fabs(static_cast<double>(heights(col, row)) - mean));
        }
    }
    return residuals;
}

/// The median of `values`, reordering them; the upper of the middle two for an even count, 0 for
/// none.
double Median(std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

/// The Hurst exponent H of `heights`, unclamped: `FitLagLine` of the median absolute difference of
/// lag pairs; none from fewer than two lags.
std::optional<double> MedianHurst(const Raster<float>& heights) {
    const DeviogramFit fit = FitLagLine(kMaxRoughnessLag, [&](std::size_t lag) {
        std::vector<double> increments;
        ForEachLagPair(heights, lag,
                       [&](double a, double b) { increments.push_back(std::fabs(b - a)); });
        return Median(increments);
    });
    return fit.lags >= 2 ? std::optional<double>(fit.slope) : std::nullopt;
}

/// Posts [`col0`, `col0` + `width`) x [`row0`, `row0` + `height`) of `posts` as a raster.
Raster<float> PostRaster(const detail::LatticeWindow<float>& posts, std::int64_t col0,
                         std::int64_t row0, std::size_t width, std::size_t height) {
    Raster<float> raster(width, height);
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t col = 0; col < width; ++col) {
            raster(col, row) = posts.At(col0 + static_cast<std::int64_t>(col),
                                        row0 + static_cast<std::int64_t>(row));
        }
    }
    return raster;
}

}  // namespace

void ValidateDiamondSquareOptions(const DiamondSquareOptions& options) {
    if (options.fractalDimension) {
        RequireFinite(*options.fractalDimension, "Diamond-square fractal dimension");
        if (*options.fractalDimension < 2.0 || *options.fractalDimension > 3.0) {
            throw std::invalid_argument("Diamond-square fractal dimension must be in [2, 3]");
        }
    }
    if (options.scale) {
        RequireNonNegative(*options.scale, "Diamond-square scale");
    }
    if (options.fitWindow != 0 &&
        (options.fitWindow < 4 || options.fitWindow > kMaxDiamondSquareFitWindow)) {
        throw std::invalid_argument("Diamond-square fit window must be 0 or 4 to " +
                                    std::to_string(kMaxDiamondSquareFitWindow) + " posts");
    }
    RequireNonNegative(options.minPixelSize, "Diamond-square min pixel size");
}

double EstimateMidpointScale(const Raster<float>& heights) {
    std::vector<double> residuals = AbsoluteResiduals(heights);
    return kGaussianMadScale * Median(residuals);
}

MidpointRoughness EstimateMidpointRoughness(const Raster<float>& heights) {
    std::vector<double> residuals = AbsoluteResiduals(heights);
    if (residuals.empty()) {
        throw std::invalid_argument(
            "Estimating midpoint roughness needs a cell whose four neighbors hold data");
    }
    MidpointRoughness roughness;
    roughness.scale = kGaussianMadScale * Median(residuals);
    if (const std::optional<double> hurst = MedianHurst(heights)) {
        roughness.fractalDimension = 3.0 - std::clamp(*hurst, 0.0, 1.0);
    }
    return roughness;
}

DiamondSquareUpsampler::DiamondSquareUpsampler(DiamondSquareOptions options)
    : options_(options),
      stepFactor_(std::pow(2.0, -(3.0 - options.fractalDimension.value_or(2.5)) / 2.0)) {
    ValidateDiamondSquareOptions(options_);
}

bool DiamondSquareUpsampler::Fitted() const {
    return options_.fractalDimension && (options_.scale || options_.fitWindow > 0);
}

int DiamondSquareUpsampler::Levels(double postSpacing, double spacing) const {
    const double ratio = postSpacing / spacing;
    if (!(ratio > 1.0)) {
        return 0;
    }
    double most = kMaxLevels;
    if (options_.minPixelSize > 0.0) {
        most = std::clamp(std::floor(std::log2(postSpacing / options_.minPixelSize) + 1e-9), 0.0,
                          static_cast<double>(kMaxLevels));
    }
    return static_cast<int>(std::min(std::ceil(std::log2(ratio) - 1e-9), most));
}

DiamondSquareUpsampler::NodeFrame DiamondSquareUpsampler::Frame(const PostLattice& posts) {
    const GeoTransform transform = posts.Transform();
    if (transform.IsRotated()) {
        throw std::invalid_argument("Diamond-square cannot refine a rotated lattice of posts");
    }
    const Vec2d first = transform.PixelToWorld(0.5, 0.5);
    return {first.x, first.y, transform.pixelWidth, -transform.pixelHeight};
}

DiamondSquareUpsampler::Nodes DiamondSquareUpsampler::BaseNodes(const PostLattice& posts,
                                                                const detail::IndexBox& box) {
    const detail::LatticeWindow<float> read =
        detail::ReadPosts(posts, Box(box.col0, -box.row1, box.col1, -box.row0));
    Nodes nodes(box);
    for (std::int64_t row = box.row0; row <= box.row1; ++row) {
        for (std::int64_t col = box.col0; col <= box.col1; ++col) {
            nodes.At(col, row) = static_cast<double>(read.At(col, -row));
        }
    }
    return nodes;
}

DiamondSquareUpsampler::ScaleField
DiamondSquareUpsampler::Scales(const PostLattice& posts, int level,
                               const detail::IndexBox& box) const {
    ScaleField scales;
    if (options_.scale || options_.fitWindow == 0) {
        scales.uniform = options_.scale.value_or(0.0);
        return scales;
    }
    scales.step = static_cast<std::int64_t>(options_.fitWindow / 2);
    const double step = static_cast<double>(scales.step);
    const auto center = [&](double post) {
        return static_cast<std::int64_t>(std::floor(post / step));
    };
    detail::IndexBox centers;
    centers.Include(center(std::ldexp(static_cast<double>(box.col0), -level) - kScaleMargin),
                    center(std::ldexp(static_cast<double>(box.col1), -level) + kScaleMargin) + 1,
                    center(std::ldexp(-static_cast<double>(box.row1), -level) - kScaleMargin),
                    center(std::ldexp(-static_cast<double>(box.row0), -level) + kScaleMargin) + 1);
    const std::size_t side = options_.fitWindow;
    const auto half = static_cast<std::int64_t>(side / 2);
    const detail::LatticeWindow<float> read = detail::ReadPosts(
        posts, Box(centers.col0 * scales.step - half, centers.row0 * scales.step - half,
                   centers.col1 * scales.step - half + static_cast<std::int64_t>(side) - 1,
                   centers.row1 * scales.step - half + static_cast<std::int64_t>(side) - 1));
    detail::LatticeWindow<double> windows(centers);
    for (std::int64_t j = centers.row0; j <= centers.row1; ++j) {
        for (std::int64_t i = centers.col0; i <= centers.col1; ++i) {
            windows.At(i, j) = EstimateMidpointScale(
                PostRaster(read, i * scales.step - half, j * scales.step - half, side, side));
        }
    }
    scales.windows = std::move(windows);
    return scales;
}

double DiamondSquareUpsampler::ScaleAt(const ScaleField& scales, std::int64_t col, std::int64_t row,
                                       int level) {
    if (!scales.windows) {
        return scales.uniform;
    }
    const detail::LatticeWindow<double>& windows = *scales.windows;
    const double step = static_cast<double>(scales.step);
    const double u = std::ldexp(static_cast<double>(col), -level) / step;
    const double v = std::ldexp(-static_cast<double>(row), -level) / step;
    const double i0 = std::floor(u);
    const double j0 = std::floor(v);
    const double s = u - i0;
    const double t = v - j0;
    const auto at = [&](double i, double j) {
        const std::int64_t c =
            std::clamp(static_cast<std::int64_t>(i), windows.box.col0, windows.box.col1);
        const std::int64_t r =
            std::clamp(static_cast<std::int64_t>(j), windows.box.row0, windows.box.row1);
        return windows.At(c, r);
    };
    const double first = at(i0, j0) * (1.0 - s) + at(i0 + 1.0, j0) * s;
    const double second = at(i0, j0 + 1.0) * (1.0 - s) + at(i0 + 1.0, j0 + 1.0) * s;
    return first * (1.0 - t) + second * t;
}

double DiamondSquareUpsampler::Displacement(std::int64_t col, std::int64_t row, int level,
                                            bool diamond, double scale) const {
    RandomStream random(options_.seed, kMidpointDisplacementStream, static_cast<std::uint32_t>(col),
                        static_cast<std::uint32_t>(row),
                        static_cast<std::uint32_t>(level) |
                            (static_cast<std::uint32_t>((col >> 32) ^ (row >> 32)) << 5));
    const double step = scale * std::pow(stepFactor_, diamond ? 2 * level : 2 * level - 1);
    return step * StandardNormal(random);
}

DiamondSquareUpsampler::Nodes DiamondSquareUpsampler::SquareStep(const Nodes& parents, int level,
                                                                 const detail::IndexBox& box,
                                                                 const ScaleField& scales) const {
    const bool spline = options_.interp == DiamondSquareInterp::CatmullRom;
    Nodes squares(box);
    for (std::int64_t row = box.row0; row <= box.row1; ++row) {
        for (std::int64_t col = box.col0; col <= box.col1; ++col) {
            if (!IsOdd(col) && !IsOdd(row)) {
                squares.At(col, row) = parents.At(col / 2, row / 2);
            } else if (IsOdd(col) && IsOdd(row)) {
                squares.At(col, row) =
                    SquareMean(parents, FloorHalf(col), FloorHalf(row), spline) +
                    Displacement(col, row, level, false, ScaleAt(scales, col, row, level));
            }
        }
    }
    return squares;
}

DiamondSquareUpsampler::Nodes DiamondSquareUpsampler::DiamondStep(const Nodes& squares, int level,
                                                                  const detail::IndexBox& box,
                                                                  const ScaleField& scales) const {
    const bool spline = options_.interp == DiamondSquareInterp::CatmullRom;
    Nodes nodes(box);
    for (std::int64_t row = box.row0; row <= box.row1; ++row) {
        for (std::int64_t col = box.col0; col <= box.col1; ++col) {
            nodes.At(col, row) =
                IsOdd(col) == IsOdd(row)
                    ? squares.At(col, row)
                    : DiamondMean(squares, col, row, spline) +
                          Displacement(col, row, level, true, ScaleAt(scales, col, row, level));
        }
    }
    return nodes;
}

DiamondSquareUpsampler::Nodes DiamondSquareUpsampler::Refine(const PostLattice& posts, int level,
                                                             const detail::IndexBox& box,
                                                             const ScaleField& scales) const {
    if (level <= 0) {
        return BaseNodes(posts, box);
    }
    const bool spline = options_.interp == DiamondSquareInterp::CatmullRom;
    const std::int64_t reach = spline ? 3 : 1;
    const std::int64_t margin = spline ? reach : 0;
    const Nodes parents =
        Refine(posts, level - 1,
               Box(FloorHalf(box.col0 - margin) - 1, FloorHalf(box.row0 - margin) - 1,
                   CeilHalf(box.col1 + margin) + 1, CeilHalf(box.row1 + margin) + 1),
               scales);
    const Nodes squares = SquareStep(
        parents, level, Box(box.col0 - reach, box.row0 - reach, box.col1 + reach, box.row1 + reach),
        scales);
    return DiamondStep(squares, level, box, scales);
}

detail::IndexBox DiamondSquareUpsampler::ReadBox(const NodeFrame& frame, int level,
                                                 Span<const Vec2d> positions) const {
    const double stepX = std::ldexp(frame.stepX, -level);
    const double stepY = std::ldexp(frame.stepY, -level);
    const std::int64_t before = options_.interp == DiamondSquareInterp::CatmullRom ? 1 : 0;
    const std::int64_t after = options_.interp == DiamondSquareInterp::CatmullRom ? 2 : 1;
    detail::IndexBox box;
    for (const Vec2d& p : positions) {
        const auto [col, u] = NodeAndShare((p.x - frame.originX) / stepX);
        const auto [row, v] = NodeAndShare((p.y - frame.originY) / stepY);
        box.Include(u > 0.0 ? col - before : col, u > 0.0 ? col + after : col,
                    v > 0.0 ? row - before : row, v > 0.0 ? row + after : row);
    }
    return box;
}

double DiamondSquareUpsampler::Interpolate(const NodeFrame& frame, const Nodes& nodes, int level,
                                           double x, double y) const {
    const auto [col, u] = NodeAndShare((x - frame.originX) / std::ldexp(frame.stepX, -level));
    const auto [row, v] = NodeAndShare((y - frame.originY) / std::ldexp(frame.stepY, -level));
    if (options_.interp == DiamondSquareInterp::CatmullRom) {
        const std::array<double, 6> wx = KernelWeights(ResampleMethod::Cubic, u);
        const std::array<double, 6> wy = KernelWeights(ResampleMethod::Cubic, v);
        double sum = 0.0;
        for (std::int64_t j = -1; j <= 2; ++j) {
            const double weightY = wy[static_cast<std::size_t>(j + 1)];
            if (weightY == 0.0) {
                continue;
            }
            for (std::int64_t i = -1; i <= 2; ++i) {
                const double weightX = wx[static_cast<std::size_t>(i + 1)];
                if (weightX != 0.0) {
                    sum += weightX * weightY * nodes.At(col + i, row + j);
                }
            }
        }
        return sum;
    }
    double value = nodes.At(col, row);
    if (u > 0.0) {
        value = value * (1.0 - u) + nodes.At(col + 1, row) * u;
    }
    if (v > 0.0) {
        double above = nodes.At(col, row + 1);
        if (u > 0.0) {
            above = above * (1.0 - u) + nodes.At(col + 1, row + 1) * u;
        }
        value = value * (1.0 - v) + above * v;
    }
    return value;
}

void DiamondSquareUpsampler::FillSamples(const PostLattice& posts, SampleBlock& samples,
                                         double spacing, unsigned threads) const {
    if (!Fitted()) {
        throw std::invalid_argument("Diamond-square needs a fractal dimension, and a scale or a "
                                    "fit window, given or estimated from the posts");
    }
    const NodeFrame frame = Frame(posts);
    const int level = Levels(PostSpacing(posts.Transform()), spacing);
    detail::FillByTiles(samples, threads, [&](Span<const Vec2d> points, Span<float> heights) {
        detail::ExpandPoints(
            points, heights, kNodesPerPoint,
            [&](Span<const Vec2d> at) { return ReadBox(frame, level, at); },
            [&](const detail::IndexBox& box, Span<const Vec2d> at, Span<float> values) {
                const Nodes nodes =
                    Refine(posts, level, box, level > 0 ? Scales(posts, level, box) : ScaleField{});
                for (std::size_t i = 0; i < at.size(); ++i) {
                    values[i] =
                        static_cast<float>(Interpolate(frame, nodes, level, at[i].x, at[i].y));
                }
            });
    });
}

int DiamondSquareUpsampler::Reach() const {
    return options_.interp == DiamondSquareInterp::CatmullRom ? 8 : 4;
}

std::shared_ptr<const Upsampler> DiamondSquareUpsampler::FitTo(const PostLattice& posts) const {
    if (Fitted()) {
        return nullptr;
    }
    detail::IndexBox box;
    if (const std::optional<PixelWindow> extent = posts.Extent()) {
        const std::size_t width = std::min(extent->colEnd - extent->colBegin, kEstimateSide);
        const std::size_t height = std::min(extent->rowEnd - extent->rowBegin, kEstimateSide);
        const auto colBegin = static_cast<std::int64_t>(
            extent->colBegin + (extent->colEnd - extent->colBegin - width) / 2);
        const auto rowBegin = static_cast<std::int64_t>(
            extent->rowBegin + (extent->rowEnd - extent->rowBegin - height) / 2);
        box = Box(colBegin, rowBegin, colBegin + static_cast<std::int64_t>(width) - 1,
                  rowBegin + static_cast<std::int64_t>(height) - 1);
    } else {
        const auto side = static_cast<std::int64_t>(kUnboundedEstimateSide);
        box = Box(0, 0, side - 1, side - 1);
    }
    const detail::LatticeWindow<float> read = detail::ReadPosts(posts, box);
    const MidpointRoughness roughness =
        EstimateMidpointRoughness(PostRaster(read, box.col0, box.row0, box.Columns(), box.Rows()));
    DiamondSquareOptions fitted = options_;
    fitted.fractalDimension = options_.fractalDimension.value_or(roughness.fractalDimension);
    if (options_.fitWindow == 0) {
        fitted.scale = options_.scale.value_or(roughness.scale);
    }
    return std::make_shared<DiamondSquareUpsampler>(fitted);
}

std::shared_ptr<const Upsampler> DiamondSquareUpsampler::Reseeded(std::uint64_t seed) const {
    DiamondSquareOptions reseeded = options_;
    reseeded.seed = ReseedWord(seed, 0);
    return std::make_shared<DiamondSquareUpsampler>(reseeded);
}

}  // namespace terraingen
