#include "terraingen/sources/diamond_square.h"

#include "terraingen/random/samplers.h"
#include "terraingen/random/stream.h"
#include "terraingen/random/streams.h"
#include "terraingen/raster/variogram.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

/// Levels the posts are refined to at most.
constexpr int kMaxLevels = 30;

/// Grid cells per side of the tiles a grid is refined in.
constexpr std::size_t kTileCells = 128;

/// How close to a node, in node spacings, a sample takes the node's height.
constexpr double kOnNode = 1e-7;

/// Largest side, in posts, of the window the roughness is estimated from.
constexpr std::size_t kEstimateSide = 512;

/// Side, in posts, of the window the roughness of a lattice without bounds is estimated from.
constexpr std::size_t kUnboundedEstimateSide = 256;

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

/// An empty window of nodes over columns [`col0`, `col1`] and rows [`row0`, `row1`].
MidpointNodes NodeWindow(std::int64_t col0, std::int64_t row0, std::int64_t col1,
                         std::int64_t row1) {
    MidpointNodes nodes;
    nodes.col0 = col0;
    nodes.row0 = row0;
    nodes.width = static_cast<std::size_t>(col1 - col0 + 1);
    nodes.height = static_cast<std::size_t>(row1 - row0 + 1);
    nodes.values.assign(nodes.width * nodes.height, 0.0);
    return nodes;
}

double& NodeAt(MidpointNodes& nodes, std::int64_t col, std::int64_t row) {
    return nodes.values[static_cast<std::size_t>(row - nodes.row0) * nodes.width +
                        static_cast<std::size_t>(col - nodes.col0)];
}

/// RMS of each cell minus the mean of its four neighbors one cell away, over the cells where all
/// five hold data, and how many cells gave it.
std::pair<double, std::size_t> ResidualRms(const Raster<float>& heights) {
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t row = 1; row + 1 < heights.Height(); ++row) {
        for (std::size_t col = 1; col + 1 < heights.Width(); ++col) {
            if (!HasData(heights, col, row) || !HasData(heights, col - 1, row) ||
                !HasData(heights, col + 1, row) || !HasData(heights, col, row - 1) ||
                !HasData(heights, col, row + 1)) {
                continue;
            }
            const double mean = 0.25 * (static_cast<double>(heights(col - 1, row)) +
                                        static_cast<double>(heights(col + 1, row)) +
                                        static_cast<double>(heights(col, row - 1)) +
                                        static_cast<double>(heights(col, row + 1)));
            const double residual = static_cast<double>(heights(col, row)) - mean;
            sum += residual * residual;
            ++count;
        }
    }
    return {count > 0 ? std::sqrt(sum / static_cast<double>(count)) : 0.0, count};
}

/// The posts [`col0`, `col0` + `width`) x [`row0`, `row0` + `height`) as a raster, NaN without
/// data.
Raster<float> ReadPosts(const PostLattice& posts, std::int64_t col0, std::int64_t row0,
                        std::size_t width, std::size_t height) {
    Raster<float> raster(width, height);
    posts.Read(col0, row0, width, height, Span<float>(raster.Values()));
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
    RequireNonNegative(options.minPixelSize, "Diamond-square min pixel size");
}

MidpointRoughness EstimateMidpointRoughness(const Raster<float>& heights) {
    const auto [residual, residualCount] = ResidualRms(heights);
    if (residualCount == 0) {
        throw std::invalid_argument(
            "Estimating midpoint roughness needs a cell whose four neighbors hold data");
    }
    MidpointRoughness roughness;
    roughness.scale = residual;
    const DeviogramFit fit = FitDeviogram(heights);
    if (fit.lags >= 2) {
        roughness.fractalDimension = 3.0 - std::clamp(fit.slope, 0.0, 1.0);
    }
    return roughness;
}

DiamondSquareUpsampler::DiamondSquareUpsampler(DiamondSquareOptions options) : options_(options) {
    ValidateDiamondSquareOptions(options_);
    stepFactor_ = std::pow(2.0, -(3.0 - options_.fractalDimension.value_or(2.5)) / 2.0);
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

int DiamondSquareUpsampler::Reach() const {
    return options_.interp == DiamondSquareInterp::CatmullRom ? 8 : 4;
}

DiamondSquareUpsampler::NodeFrame DiamondSquareUpsampler::Frame(const PostLattice& posts) {
    const GeoTransform transform = posts.Transform();
    if (transform.IsRotated()) {
        throw std::invalid_argument("Diamond-square cannot refine a rotated lattice of posts");
    }
    const Vec2d first = transform.PixelToWorld(0.5, 0.5);
    return {first.x, first.y, transform.pixelWidth, -transform.pixelHeight};
}

double DiamondSquareUpsampler::StepScale(int level, bool diamond) const {
    return *options_.scale * std::pow(stepFactor_, diamond ? 2 * level : 2 * level - 1);
}

MidpointNodes DiamondSquareUpsampler::BaseNodes(const PostLattice& posts, std::int64_t col0,
                                                std::int64_t row0, std::int64_t col1,
                                                std::int64_t row1) {
    MidpointNodes nodes = NodeWindow(col0, row0, col1, row1);
    const Raster<float> read = ReadPosts(posts, col0, -row1, nodes.width, nodes.height);
    for (std::int64_t row = row0; row <= row1; ++row) {
        for (std::int64_t col = col0; col <= col1; ++col) {
            NodeAt(nodes, col, row) = static_cast<double>(
                read(static_cast<std::size_t>(col - col0), static_cast<std::size_t>(row1 - row)));
        }
    }
    return nodes;
}

MidpointNodes DiamondSquareUpsampler::Nodes(const PostLattice& posts, int level, std::int64_t col0,
                                            std::int64_t row0, std::int64_t col1,
                                            std::int64_t row1) const {
    if (!options_.fractalDimension || !options_.scale) {
        throw std::invalid_argument("Diamond-square needs a fractal dimension and scale, given or "
                                    "estimated from the posts");
    }
    Frame(posts);
    if (level <= 0) {
        return BaseNodes(posts, col0, row0, col1, row1);
    }
    const bool spline = options_.interp == DiamondSquareInterp::CatmullRom;
    const std::int64_t reach = spline ? 3 : 1;
    const MidpointNodes parents =
        spline ? Nodes(posts, level - 1, FloorHalf(col0 - reach) - 1, FloorHalf(row0 - reach) - 1,
                       CeilHalf(col1 + reach) + 1, CeilHalf(row1 + reach) + 1)
               : Nodes(posts, level - 1, FloorHalf(col0) - 1, FloorHalf(row0) - 1,
                       CeilHalf(col1) + 1, CeilHalf(row1) + 1);
    const auto displacement = [&](std::int64_t col, std::int64_t row, bool diamond) {
        RandomStream random(options_.seed, kMidpointDisplacementStream,
                            static_cast<std::uint32_t>(col), static_cast<std::uint32_t>(row),
                            static_cast<std::uint32_t>(level) |
                                (static_cast<std::uint32_t>((col >> 32) ^ (row >> 32)) << 5));
        return StepScale(level, diamond) * StandardNormal(random);
    };
    const auto square = [&](std::int64_t left, std::int64_t bottom) {
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
    };

    MidpointNodes expanded = NodeWindow(col0 - reach, row0 - reach, col1 + reach, row1 + reach);
    for (std::int64_t row = row0 - reach; row <= row1 + reach; ++row) {
        for (std::int64_t col = col0 - reach; col <= col1 + reach; ++col) {
            double& node = NodeAt(expanded, col, row);
            if (!IsOdd(col) && !IsOdd(row)) {
                node = parents.At(col / 2, row / 2);
            } else if (IsOdd(col) && IsOdd(row)) {
                node = square(FloorHalf(col), FloorHalf(row)) + displacement(col, row, false);
            }
        }
    }
    const auto diamond = [&](std::int64_t col, std::int64_t row) {
        if (!spline) {
            return 0.25 * (expanded.At(col - 1, row) + expanded.At(col + 1, row) +
                           expanded.At(col, row - 1) + expanded.At(col, row + 1));
        }
        double alongX = 0.0;
        double alongY = 0.0;
        for (std::int64_t k = 0; k < 4; ++k) {
            const double weight = kMidpointWeights[static_cast<std::size_t>(k)];
            alongX += weight * expanded.At(col - 3 + 2 * k, row);
            alongY += weight * expanded.At(col, row - 3 + 2 * k);
        }
        return 0.5 * (alongX + alongY);
    };
    MidpointNodes nodes = NodeWindow(col0, row0, col1, row1);
    for (std::int64_t row = row0; row <= row1; ++row) {
        for (std::int64_t col = col0; col <= col1; ++col) {
            double& node = NodeAt(nodes, col, row);
            if (IsOdd(col) == IsOdd(row)) {
                node = expanded.At(col, row);
                continue;
            }
            node = diamond(col, row) + displacement(col, row, true);
        }
    }
    return nodes;
}

void DiamondSquareUpsampler::Window(const NodeFrame& frame, int level,
                                    const std::vector<Vec2d>& positions, std::int64_t& col0,
                                    std::int64_t& row0, std::int64_t& col1,
                                    std::int64_t& row1) const {
    const double stepX = std::ldexp(frame.stepX, -level);
    const double stepY = std::ldexp(frame.stepY, -level);
    const std::int64_t before = options_.interp == DiamondSquareInterp::CatmullRom ? 1 : 0;
    const std::int64_t after = options_.interp == DiamondSquareInterp::CatmullRom ? 2 : 1;
    col0 = std::numeric_limits<std::int64_t>::max();
    row0 = std::numeric_limits<std::int64_t>::max();
    col1 = std::numeric_limits<std::int64_t>::min();
    row1 = std::numeric_limits<std::int64_t>::min();
    for (const Vec2d& p : positions) {
        const auto [col, u] = NodeAndShare((p.x - frame.originX) / stepX);
        const auto [row, v] = NodeAndShare((p.y - frame.originY) / stepY);
        col0 = std::min(col0, u > 0.0 ? col - before : col);
        row0 = std::min(row0, v > 0.0 ? row - before : row);
        col1 = std::max(col1, u > 0.0 ? col + after : col);
        row1 = std::max(row1, v > 0.0 ? row + after : row);
    }
}

double DiamondSquareUpsampler::Interpolate(const NodeFrame& frame, const MidpointNodes& nodes,
                                           int level, double x, double y) const {
    const auto [col, u] = NodeAndShare((x - frame.originX) / std::ldexp(frame.stepX, -level));
    const auto [row, v] = NodeAndShare((y - frame.originY) / std::ldexp(frame.stepY, -level));
    if (options_.interp == DiamondSquareInterp::CatmullRom) {
        const std::array<double, 6> wx = KernelWeights(ResampleMethod::CatmullRom, u);
        const std::array<double, 6> wy = KernelWeights(ResampleMethod::CatmullRom, v);
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

void DiamondSquareUpsampler::Expand(const PostLattice& posts, const std::vector<Vec2d>& positions,
                                    std::vector<float>& heights, double spacing) const {
    heights.resize(positions.size());
    if (positions.empty()) {
        return;
    }
    const NodeFrame frame = Frame(posts);
    const int level = Levels(PostSpacing(posts.Transform()), spacing);
    std::int64_t col0 = 0;
    std::int64_t row0 = 0;
    std::int64_t col1 = 0;
    std::int64_t row1 = 0;
    Window(frame, level, positions, col0, row0, col1, row1);
    const MidpointNodes nodes = Nodes(posts, level, col0, row0, col1, row1);
    for (std::size_t i = 0; i < positions.size(); ++i) {
        heights[i] =
            static_cast<float>(Interpolate(frame, nodes, level, positions[i].x, positions[i].y));
    }
}

float DiamondSquareUpsampler::HeightAt(const PostLattice& posts, double x, double y,
                                       double spacing) const {
    std::vector<float> height;
    Expand(posts, {Vec2d{x, y}}, height, spacing);
    return height[0];
}

void DiamondSquareUpsampler::Fill(const PostLattice& posts, Raster<float>& heights,
                                  double spacing) const {
    const NodeFrame frame = Frame(posts);
    const int level = Levels(PostSpacing(posts.Transform()), spacing);
    const std::size_t tilesX = (heights.Width() + kTileCells - 1) / kTileCells;
    const std::size_t tilesY = (heights.Height() + kTileCells - 1) / kTileCells;
    ParallelFor(tilesX * tilesY, 0, [&](std::size_t tile) {
        const std::size_t colBegin = (tile % tilesX) * kTileCells;
        const std::size_t rowBegin = (tile / tilesX) * kTileCells;
        const std::size_t colEnd = std::min(colBegin + kTileCells, heights.Width());
        const std::size_t rowEnd = std::min(rowBegin + kTileCells, heights.Height());
        std::vector<Vec2d> positions;
        for (std::size_t row = rowBegin; row < rowEnd; ++row) {
            for (std::size_t col = colBegin; col < colEnd; ++col) {
                positions.push_back(heights.CellCenter(col, row));
            }
        }
        std::int64_t col0 = 0;
        std::int64_t row0 = 0;
        std::int64_t col1 = 0;
        std::int64_t row1 = 0;
        Window(frame, level, positions, col0, row0, col1, row1);
        const double nodes =
            static_cast<double>(col1 - col0 + 1) * static_cast<double>(row1 - row0 + 1);
        std::vector<float> values;
        if (nodes <= 64.0 * static_cast<double>(positions.size())) {
            Expand(posts, positions, values, spacing);
        } else {
            for (const Vec2d& p : positions) {
                values.push_back(HeightAt(posts, p.x, p.y, spacing));
            }
        }
        std::size_t n = 0;
        for (std::size_t row = rowBegin; row < rowEnd; ++row) {
            for (std::size_t col = colBegin; col < colEnd; ++col) {
                heights(col, row) = values[n++];
            }
        }
    });
}

std::shared_ptr<const Upsampler> DiamondSquareUpsampler::FitTo(const PostLattice& posts) const {
    if (options_.fractalDimension && options_.scale) {
        return nullptr;
    }
    Raster<float> sample;
    if (const std::optional<PixelWindow> extent = posts.Extent()) {
        const std::size_t width = std::min(extent->colEnd - extent->colBegin, kEstimateSide);
        const std::size_t height = std::min(extent->rowEnd - extent->rowBegin, kEstimateSide);
        const std::size_t colBegin =
            extent->colBegin + (extent->colEnd - extent->colBegin - width) / 2;
        const std::size_t rowBegin =
            extent->rowBegin + (extent->rowEnd - extent->rowBegin - height) / 2;
        sample = ReadPosts(posts, static_cast<std::int64_t>(colBegin),
                           static_cast<std::int64_t>(rowBegin), width, height);
    } else {
        sample = ReadPosts(posts, 0, 0, kUnboundedEstimateSide, kUnboundedEstimateSide);
    }
    const MidpointRoughness roughness = EstimateMidpointRoughness(sample);
    DiamondSquareOptions fitted = options_;
    fitted.fractalDimension = options_.fractalDimension.value_or(roughness.fractalDimension);
    fitted.scale = options_.scale.value_or(roughness.scale);
    return std::make_shared<DiamondSquareUpsampler>(fitted);
}

}  // namespace terraingen
