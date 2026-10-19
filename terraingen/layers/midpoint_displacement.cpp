#include "terraingen/layers/midpoint_displacement.h"

#include "terraingen/layers/sample_block.h"
#include "terraingen/random/samplers.h"
#include "terraingen/random/stream.h"
#include "terraingen/random/streams.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

/// Levels a lattice expands to at most.
constexpr int kMaxLevels = 30;

/// Grid cells per side of the tiles a grid is expanded in.
constexpr std::size_t kTileCells = 128;

/// The longest lag, in cells, the fractal dimension is fitted over.
constexpr std::size_t kMaxVariogramLag = 8;

/// How close to a node, in node spacings, a sample takes the node's height.
constexpr double kOnNode = 1e-7;

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

/// Whether cell (`col`, `row`) of `heights` holds data.
bool HasData(const Raster<float>& heights, std::size_t col, std::size_t row) {
    const float value = heights(col, row);
    return std::isfinite(value) && !heights.IsNoData(value);
}

/// RMS of each cell minus the mean of its four neighbours one cell away, over the cells where all
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

/// The mean squared difference of cells `lag` apart along rows and columns where both hold data,
/// and how many pairs gave it.
std::pair<double, std::size_t> Variogram(const Raster<float>& heights, std::size_t lag) {
    double sum = 0.0;
    std::size_t count = 0;
    const auto add = [&](std::size_t col, std::size_t row, std::size_t col2, std::size_t row2) {
        if (HasData(heights, col, row) && HasData(heights, col2, row2)) {
            const double difference =
                static_cast<double>(heights(col2, row2)) - static_cast<double>(heights(col, row));
            sum += difference * difference;
            ++count;
        }
    };
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            if (col + lag < heights.Width()) {
                add(col, row, col + lag, row);
            }
            if (row + lag < heights.Height()) {
                add(col, row, col, row + lag);
            }
        }
    }
    return {count > 0 ? sum / static_cast<double>(count) : 0.0, count};
}

}  // namespace

void ValidateMidpointDisplacementOptions(const MidpointDisplacementOptions& options) {
    RequirePositive(options.pixelSize, "Midpoint displacement pixel size");
    RequireFinite(options.origin.x, "Midpoint displacement origin x");
    RequireFinite(options.origin.y, "Midpoint displacement origin y");
    RequireFinite(options.fractalDimension, "Midpoint displacement fractal dimension");
    if (options.fractalDimension < 2.0 || options.fractalDimension > 3.0) {
        throw std::invalid_argument("Midpoint displacement fractal dimension must be in [2, 3]");
    }
    RequireNonNegative(options.scale, "Midpoint displacement scale");
    RequireNonNegative(options.minPixelSize, "Midpoint displacement min pixel size");
}

MidpointRoughness EstimateMidpointRoughness(const Raster<float>& heights) {
    const auto [residual, residualCount] = ResidualRms(heights);
    if (residualCount == 0) {
        throw std::invalid_argument(
            "Estimating midpoint roughness needs a cell whose four neighbours hold data");
    }
    MidpointRoughness roughness;
    roughness.scale = residual;
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXX = 0.0;
    double sumXY = 0.0;
    int lags = 0;
    for (std::size_t lag = 1; lag <= kMaxVariogramLag; lag *= 2) {
        const auto [variogram, pairs] = Variogram(heights, lag);
        if (pairs == 0 || !(variogram > 0.0)) {
            break;
        }
        const double x = std::log2(static_cast<double>(lag));
        const double y = 0.5 * std::log2(variogram);
        sumX += x;
        sumY += y;
        sumXX += x * x;
        sumXY += x * y;
        ++lags;
    }
    if (lags >= 2) {
        const double hurst = (lags * sumXY - sumX * sumY) / (lags * sumXX - sumX * sumX);
        roughness.fractalDimension = 3.0 - std::clamp(hurst, 0.0, 1.0);
    }
    return roughness;
}

MidpointDisplacementLayer::MidpointDisplacementLayer(MidpointDisplacementOptions options,
                                                     Surface surface)
    : options_(options), surface_(surface) {
    ValidateMidpointDisplacementOptions(options_);
    maxLevels_ = kMaxLevels;
    if (options_.minPixelSize > 0.0) {
        const double levels =
            std::floor(std::log2(options_.pixelSize / options_.minPixelSize) + 1e-9);
        maxLevels_ = static_cast<int>(std::clamp(levels, 0.0, static_cast<double>(kMaxLevels)));
    }
    stepFactor_ = std::pow(2.0, -(3.0 - options_.fractalDimension) / 2.0);
}

int MidpointDisplacementLayer::Levels(double spacing) const {
    const double ratio = options_.pixelSize * surface_.UnitLength() / spacing;
    if (!(ratio > 1.0)) {
        return 0;
    }
    const double levels = std::ceil(std::log2(ratio) - 1e-9);
    return static_cast<int>(std::min(levels, static_cast<double>(maxLevels_)));
}

double MidpointDisplacementLayer::NodeSpacing(int level) const {
    return std::ldexp(options_.pixelSize, -level);
}

double MidpointDisplacementLayer::StepScale(int level, bool diamond) const {
    return options_.scale * std::pow(stepFactor_, diamond ? 2 * level : 2 * level - 1);
}

MidpointNodes MidpointDisplacementLayer::BaseNodes(std::int64_t col0, std::int64_t row0,
                                                   std::int64_t col1, std::int64_t row1,
                                                   const Ground* ground) const {
    MidpointNodes nodes = NodeWindow(col0, row0, col1, row1);
    if (options_.base == MidpointBase::Source) {
        return nodes;
    }
    RequireGround(ground, "Midpoint displacement from the ground");
    std::vector<Vec2d> points;
    points.reserve(nodes.values.size());
    for (std::int64_t row = row0; row <= row1; ++row) {
        for (std::int64_t col = col0; col <= col1; ++col) {
            points.push_back({options_.origin.x + static_cast<double>(col) * options_.pixelSize,
                              options_.origin.y + static_cast<double>(row) * options_.pixelSize});
        }
    }
    std::vector<float> heights(points.size());
    ground->HeightsAt(Span<const Vec2d>(points), Span<float>(heights),
                      options_.pixelSize * surface_.UnitLength());
    for (std::size_t i = 0; i < heights.size(); ++i) {
        nodes.values[i] = static_cast<double>(heights[i]);
    }
    return nodes;
}

MidpointNodes MidpointDisplacementLayer::Nodes(int level, std::int64_t col0, std::int64_t row0,
                                               std::int64_t col1, std::int64_t row1,
                                               const Ground* ground) const {
    if (level <= 0) {
        return BaseNodes(col0, row0, col1, row1, ground);
    }
    const MidpointNodes parents = Nodes(level - 1, FloorHalf(col0) - 1, FloorHalf(row0) - 1,
                                        CeilHalf(col1) + 1, CeilHalf(row1) + 1, ground);
    const auto displacement = [&](std::int64_t col, std::int64_t row, bool diamond) {
        RandomStream random(options_.seed, kMidpointDisplacementStream,
                            static_cast<std::uint32_t>(col), static_cast<std::uint32_t>(row),
                            static_cast<std::uint32_t>(level) |
                                (static_cast<std::uint32_t>((col >> 32) ^ (row >> 32)) << 5));
        return StepScale(level, diamond) * StandardNormal(random);
    };

    MidpointNodes expanded = NodeWindow(col0 - 1, row0 - 1, col1 + 1, row1 + 1);
    for (std::int64_t row = row0 - 1; row <= row1 + 1; ++row) {
        for (std::int64_t col = col0 - 1; col <= col1 + 1; ++col) {
            double& node = NodeAt(expanded, col, row);
            if (!IsOdd(col) && !IsOdd(row)) {
                node = parents.At(col / 2, row / 2);
            } else if (IsOdd(col) && IsOdd(row)) {
                const std::int64_t left = FloorHalf(col);
                const std::int64_t bottom = FloorHalf(row);
                node = 0.25 * (parents.At(left, bottom) + parents.At(left + 1, bottom) +
                               parents.At(left, bottom + 1) + parents.At(left + 1, bottom + 1)) +
                       displacement(col, row, false);
            }
        }
    }
    MidpointNodes nodes = NodeWindow(col0, row0, col1, row1);
    for (std::int64_t row = row0; row <= row1; ++row) {
        for (std::int64_t col = col0; col <= col1; ++col) {
            double& node = NodeAt(nodes, col, row);
            if (IsOdd(col) == IsOdd(row)) {
                node = expanded.At(col, row);
                continue;
            }
            node = 0.25 * (expanded.At(col - 1, row) + expanded.At(col + 1, row) +
                           expanded.At(col, row - 1) + expanded.At(col, row + 1)) +
                   displacement(col, row, true);
        }
    }
    return nodes;
}

double MidpointDisplacementLayer::Interpolate(const MidpointNodes& nodes, int level, double x,
                                              double y) const {
    const double step = NodeSpacing(level);
    const auto [col, u] = NodeAndShare((x - options_.origin.x) / step);
    const auto [row, v] = NodeAndShare((y - options_.origin.y) / step);
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

void MidpointDisplacementLayer::Expand(const std::vector<Vec2d>& positions,
                                       std::vector<float*>& heights, int level,
                                       const Ground* ground) const {
    if (positions.empty()) {
        return;
    }
    const double step = NodeSpacing(level);
    std::int64_t col0 = std::numeric_limits<std::int64_t>::max();
    std::int64_t row0 = std::numeric_limits<std::int64_t>::max();
    std::int64_t col1 = std::numeric_limits<std::int64_t>::min();
    std::int64_t row1 = std::numeric_limits<std::int64_t>::min();
    for (const Vec2d& p : positions) {
        const auto [col, u] = NodeAndShare((p.x - options_.origin.x) / step);
        const auto [row, v] = NodeAndShare((p.y - options_.origin.y) / step);
        col0 = std::min(col0, col);
        row0 = std::min(row0, row);
        col1 = std::max(col1, u > 0.0 ? col + 1 : col);
        row1 = std::max(row1, v > 0.0 ? row + 1 : row);
    }
    const MidpointNodes nodes = Nodes(level, col0, row0, col1, row1, ground);
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const double value = Interpolate(nodes, level, positions[i].x, positions[i].y);
        if (options_.base == MidpointBase::Source) {
            *heights[i] = static_cast<float>(static_cast<double>(*heights[i]) + value);
        } else if (!std::isnan(value)) {
            *heights[i] = static_cast<float>(value);
        }
    }
}

void MidpointDisplacementLayer::ApplySamples(SampleBlock& samples,
                                             const LayerContext& context) const {
    const int level = Levels(samples.ResolvedSpacing(context.spacing, surface_));
    if (level == 0) {
        return;
    }
    if (options_.base == MidpointBase::DiamondSquare) {
        RequireGround(context.ground, "Midpoint displacement from the ground");
    }
    Raster<float>* grid = samples.Grid();
    if (!grid) {
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            if (samples.IsNoData(i)) {
                continue;
            }
            std::vector<float*> height{&samples.Height(i)};
            Expand({samples.Position(i)}, height, level, context.ground);
        }
        return;
    }
    const std::size_t tilesX = (grid->Width() + kTileCells - 1) / kTileCells;
    const std::size_t tilesY = (grid->Height() + kTileCells - 1) / kTileCells;
    ParallelFor(tilesX * tilesY, context.threads, [&](std::size_t tile) {
        const std::size_t colBegin = (tile % tilesX) * kTileCells;
        const std::size_t rowBegin = (tile / tilesX) * kTileCells;
        const std::size_t colEnd = std::min(colBegin + kTileCells, grid->Width());
        const std::size_t rowEnd = std::min(rowBegin + kTileCells, grid->Height());
        std::vector<Vec2d> positions;
        std::vector<float*> heights;
        for (std::size_t row = rowBegin; row < rowEnd; ++row) {
            for (std::size_t col = colBegin; col < colEnd; ++col) {
                float& height = (*grid)(col, row);
                if (std::isnan(height) || grid->IsNoData(height)) {
                    continue;
                }
                positions.push_back(grid->CellCenter(col, row));
                heights.push_back(&height);
            }
        }
        Expand(positions, heights, level, context.ground);
    });
}

}  // namespace terraingen
