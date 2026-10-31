// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/upsampling.h"

#include "raster/kernels.h"
#include "sources/lattice.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

/// How far below the post spacing, as a share of it, a spacing still counts as the post spacing.
constexpr double kSpacingTolerance = 1e-9;

/// Rows of posts read at once when a grid on the lattice copies them.
constexpr std::size_t kStripRows = 64;

/// The post at world (`x`, `y`) of a lattice placed by `transform`, if (`x`, `y`) is one.
std::optional<std::pair<std::int64_t, std::int64_t>> CenteredPost(const GeoTransform& transform,
                                                                  double x, double y) {
    const Vec2d pixel = transform.WorldToPixel(x, y);
    const std::optional<std::int64_t> col = detail::NearestWhole(pixel.x - 0.5);
    const std::optional<std::int64_t> row = detail::NearestWhole(pixel.y - 0.5);
    if (!col || !row) {
        return std::nullopt;
    }
    return std::pair{*col, *row};
}

/// The first and last posts along an axis the kernel reads around lattice position `u`.
std::pair<std::int64_t, std::int64_t> Taps(ResampleMethod method, double u) {
    if (method == ResampleMethod::Nearest) {
        const auto nearest = static_cast<std::int64_t>(std::round(u));
        return {nearest, nearest};
    }
    const auto below = static_cast<std::int64_t>(std::floor(u));
    const int radius = KernelRadius(method);
    return {below + 1 - radius, below + radius};
}

/// The posts the kernel reads around the lattice positions `lattice`.
detail::IndexBox TapBox(ResampleMethod method, Span<const Vec2d> lattice) {
    detail::IndexBox box;
    for (const Vec2d& at : lattice) {
        const auto [first, last] = Taps(method, at.x);
        const auto [top, bottom] = Taps(method, at.y);
        box.Include(first, last, top, bottom);
    }
    return box;
}

/// `posts` interpolated with `method` at lattice position (`u`, `v`), read from `block`.
float Interpolate(const detail::LatticeWindow<float>& block, ResampleMethod method, double u,
                  double v) {
    const std::optional<float> value = InterpolateLattice(
        u, v, method,
        [&](double col, double row) {
            return block.At(static_cast<std::int64_t>(col), static_cast<std::int64_t>(row));
        },
        [](float post) { return std::isnan(post); });
    return value.value_or(std::numeric_limits<float>::quiet_NaN());
}

/// The cell `KernelWeights` start from along an axis and the weights there, at lattice position
/// `u`.
struct AxisWeights {
    double at = 0.0;
    double first = 0.0;
    std::array<double, 6> weights{};
};

AxisWeights WeightsAt(ResampleMethod method, double u) {
    const double first = std::floor(u);
    return {u, first, KernelWeights(method, u - first)};
}

/// Sets `heights` to `block` interpolated at the lattice positions `lattice`, to the bits of
/// `Interpolate`. The weights along x of the first run of positions sharing a row, and along y of
/// each row, serve every position below them that matches them, as on a grid aligned with the
/// posts.
void InterpolateBlock(const detail::LatticeWindow<float>& block, ResampleMethod method,
                      Span<const Vec2d> lattice, Span<float> heights) {
    if (method == ResampleMethod::Nearest) {
        for (std::size_t i = 0; i < lattice.size(); ++i) {
            heights[i] = Interpolate(block, method, lattice[i].x, lattice[i].y);
        }
        return;
    }
    std::size_t width = 1;
    while (width < lattice.size() && lattice[width].y == lattice[0].y) {
        ++width;
    }
    std::vector<AxisWeights> columns(width);
    for (std::size_t c = 0; c < width; ++c) {
        columns[c] = WeightsAt(method, lattice[c].x);
    }
    const int radius = KernelRadius(method);
    const auto value = [&](double col, double row) {
        return block.At(static_cast<std::int64_t>(col), static_cast<std::int64_t>(row));
    };
    const auto missing = [](float post) { return std::isnan(post); };
    AxisWeights row = WeightsAt(method, lattice[0].y);
    for (std::size_t i = 0; i < lattice.size(); ++i) {
        const Vec2d& at = lattice[i];
        const AxisWeights& column = columns[i % width];
        if (at.y != row.at) {
            row = WeightsAt(method, at.y);
        }
        if (at.x != column.at) {
            heights[i] = Interpolate(block, method, at.x, at.y);
            continue;
        }
        heights[i] = InterpolateLatticeWeights(column.first, row.first, column.weights, row.weights,
                                               radius, value, missing)
                         .value_or(std::numeric_limits<float>::quiet_NaN());
    }
}

/// The lattice transform of `posts`, which must not be rotated.
/// @throws std::invalid_argument for a rotated lattice.
GeoTransform UnrotatedTransform(const PostLattice& posts) {
    GeoTransform transform = posts.Transform();
    if (transform.IsRotated()) {
        throw std::invalid_argument("Upsampling cannot interpolate a rotated lattice of posts");
    }
    return transform;
}

/// Sets the cells of `heights`, a grid on the lattice of `posts` starting at `offset`, to the
/// posts under them, a strip of rows at a time, strips split over threads.
void CopyPosts(const PostLattice& posts, detail::LatticeOffset offset, Raster<float>& heights,
               unsigned threads) {
    const std::size_t width = heights.Width();
    const std::size_t height = heights.Height();
    ParallelFor(CeilDiv(height, kStripRows), threads, [&](std::size_t strip) {
        const std::size_t first = strip * kStripRows;
        const std::size_t rows = std::min(kStripRows, height - first);
        posts.Read(offset.col, offset.row + static_cast<std::int64_t>(first), width, rows,
                   heights.Values().subspan(first * width, rows * width));
    });
}

/// Sets the cells of `samples`, a grid, at the posts' spacing or coarser: copies the posts on
/// their lattice, otherwise `upsample(samples)` then gives the cells centered on posts the posts'
/// heights.
template <typename Upsample>
void FillGridAtPostSpacing(const PostLattice& posts, SampleBlock& samples, unsigned threads,
                           const Upsample& upsample) {
    Raster<float>& heights = *samples.Grid();
    const GeoTransform transform = posts.Transform();
    if (const auto offset = detail::OffsetOnLattice(transform, heights.Transform())) {
        CopyPosts(posts, *offset, heights, threads);
        return;
    }
    upsample(samples);
    ParallelFor(heights.Height(), threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const Vec2d center = heights.CellCenter(col, row);
            if (const auto post = CenteredPost(transform, center.x, center.y)) {
                posts.Read(post->first, post->second, 1, 1, Span<float>(&heights(col, row), 1));
            }
        }
    });
}

/// Sets the points of `samples` at the posts' spacing or coarser: those centered on posts to the
/// posts' heights, the others by `upsample` of a block of them at once.
template <typename Upsample>
void FillPointsAtPostSpacing(const PostLattice& posts, SampleBlock& samples,
                             const Upsample& upsample) {
    const GeoTransform transform = posts.Transform();
    const Span<const Vec2d> points = samples.Points();
    const Span<float> heights = samples.Heights();
    detail::EvaluateChosen(
        points.size(),
        [&](std::size_t i) -> std::optional<Vec2d> {
            if (const auto post = CenteredPost(transform, points[i].x, points[i].y)) {
                posts.Read(post->first, post->second, 1, 1, heights.subspan(i, 1));
                return std::nullopt;
            }
            return points[i];
        },
        [&](Span<const Vec2d> between, Span<float> values) {
            SampleBlock rest(between, values);
            upsample(rest);
        },
        [&](std::size_t i, float value) { heights[i] = value; });
}

/// The kernel upsamplers' names and kernels.
const std::array<std::pair<const char*, ResampleMethod>, 6> kKernelNames{{
    {"nearest", ResampleMethod::Nearest},
    {"bilinear", ResampleMethod::Bilinear},
    {"cubic", ResampleMethod::Cubic},
    {"catmull_rom", ResampleMethod::Cubic},
    {"lanczos3", ResampleMethod::Lanczos3},
    {"cubic_bspline", ResampleMethod::CubicBSpline},
}};

/// The upsampler types registered, by name.
struct Registry {
    std::mutex mutex;
    std::map<std::string, UpsamplerFactory> factories;
};

Registry& UpsamplerRegistry() {
    static Registry registry;
    return registry;
}

}  // namespace

namespace detail {

LatticeWindow<float> ReadPosts(const PostLattice& posts, const IndexBox& box) {
    LatticeWindow<float> window(box);
    posts.Read(box.col0, box.row0, box.Columns(), box.Rows(), Span<float>(window.values));
    return window;
}

std::optional<LatticeOffset> OffsetOnLattice(const GeoTransform& lattice,
                                             const GeoTransform& grid) {
    const auto same = [](double a, double b) { return std::fabs(a - b) <= 1e-12 * std::fabs(b); };
    if (!same(grid.pixelWidth, lattice.pixelWidth) ||
        !same(grid.pixelHeight, lattice.pixelHeight) || grid.rowRotation != lattice.rowRotation ||
        grid.colRotation != lattice.colRotation) {
        return std::nullopt;
    }
    const Vec2d corner = lattice.WorldToPixel(grid.originX, grid.originY);
    const std::optional<std::int64_t> col = detail::NearestWhole(corner.x);
    const std::optional<std::int64_t> row = detail::NearestWhole(corner.y);
    if (!col || !row) {
        return std::nullopt;
    }
    return LatticeOffset{*col, *row};
}

}  // namespace detail

RasterPosts::RasterPosts(std::shared_ptr<const Raster<float>> window,
                         const RasterPlacement& placement, bool wrapColumns)
    : window_(std::move(window)), placement_(placement), wrapColumns_(wrapColumns) {
    if (!window_ || window_->Empty()) {
        throw std::invalid_argument("Raster posts need a raster with cells");
    }
    if (wrapColumns_ && (placement_.col != 0 || window_->Width() != placement_.whole.width)) {
        throw std::invalid_argument("Wrapping raster posts must span every column");
    }
}

std::optional<PixelWindow> RasterPosts::Extent() const {
    return PixelWindow{placement_.col, placement_.col + window_->Width(), placement_.row,
                       placement_.row + window_->Height()};
}

void RasterPosts::Read(std::int64_t col0, std::int64_t row0, std::size_t width, std::size_t height,
                       Span<float> heights) const {
    const Raster<float>& window = *window_;
    const std::vector<std::size_t> cols =
        detail::PostIndices(col0, width, wrapColumns_, placement_.whole.width, placement_.col,
                            placement_.col + window.Width());
    const std::vector<std::size_t> rows =
        detail::PostIndices(row0, height, false, placement_.whole.height, placement_.row,
                            placement_.row + window.Height());
    for (std::size_t r = 0; r < height; ++r) {
        for (std::size_t c = 0; c < width; ++c) {
            const float value = window(cols[c] - placement_.col, rows[r] - placement_.row);
            heights[r * width + c] =
                window.IsMissing(value) ? std::numeric_limits<float>::quiet_NaN() : value;
        }
    }
}

double PostSpacing(const GeoTransform& transform) {
    return std::max(transform.CellWidth(), transform.CellHeight());
}

bool AtPostSpacing(const GeoTransform& transform, double spacing) {
    const double cellSize = std::sqrt(std::fabs(transform.pixelWidth * transform.pixelHeight -
                                                transform.rowRotation * transform.colRotation));
    return spacing >= cellSize * (1.0 - kSpacingTolerance);
}

void Upsampler::Fill(const PostLattice& posts, SampleBlock& samples, double spacing,
                     unsigned threads) const {
    const auto upsample = [&](SampleBlock& block) { FillSamples(posts, block, spacing, threads); };
    if (samples.Size() == 0) {
        return;
    }
    if (!AtPostSpacing(posts.Transform(), spacing)) {
        upsample(samples);
    } else if (samples.IsGrid()) {
        FillGridAtPostSpacing(posts, samples, threads, upsample);
    } else {
        FillPointsAtPostSpacing(posts, samples, upsample);
    }
}

void Upsampler::Fill(const PostLattice& posts, Raster<float>& heights, double spacing,
                     unsigned threads) const {
    SampleBlock samples(heights);
    Fill(posts, samples, spacing, threads);
}

float Upsampler::HeightAt(const PostLattice& posts, double x, double y, double spacing) const {
    return detail::PointHeight(x, y,
                               [&](SampleBlock& samples) { Fill(posts, samples, spacing, 1); });
}

void Upsampler::HeightsAt(const PostLattice& posts, Span<const Vec2d> points, Span<float> heights,
                          double spacing) const {
    SampleBlock samples(points, heights);
    Fill(posts, samples, spacing, 1);
}

void KernelUpsampler::FillSamples(const PostLattice& posts, SampleBlock& samples,
                                  double /*spacing*/, unsigned threads) const {
    const GeoTransform transform = UnrotatedTransform(posts);
    detail::FillByTiles(samples, threads, [&](Span<const Vec2d> points, Span<float> heights) {
        std::vector<Vec2d> lattice(points.size());
        for (std::size_t i = 0; i < points.size(); ++i) {
            const Vec2d pixel = transform.WorldToPixel(points[i].x, points[i].y);
            lattice[i] = {pixel.x - 0.5, pixel.y - 0.5};
        }
        const auto taps = static_cast<double>(2 * KernelRadius(method_));
        detail::ExpandPoints(
            Span<const Vec2d>(lattice), heights, 4.0 * taps * taps,
            [&](Span<const Vec2d> at) { return TapBox(method_, at); },
            [&](const detail::IndexBox& box, Span<const Vec2d> at, Span<float> values) {
                InterpolateBlock(detail::ReadPosts(posts, box), method_, at, values);
            });
    });
}

std::shared_ptr<const Upsampler> DefaultUpsampler() {
    static const std::shared_ptr<const Upsampler> cubic = std::make_shared<KernelUpsampler>();
    return cubic;
}

std::shared_ptr<const Upsampler> FittedUpsampler(std::shared_ptr<const Upsampler> upsampler,
                                                 const PostLattice& posts) {
    if (!upsampler) {
        upsampler = DefaultUpsampler();
    }
    std::shared_ptr<const Upsampler> fitted = upsampler->FitTo(posts);
    return fitted ? fitted : upsampler;
}

const std::vector<std::string>& KernelUpsamplerNames() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> list;
        for (const auto& entry : kKernelNames) {
            list.emplace_back(entry.first);
        }
        return list;
    }();
    return names;
}

std::optional<ResampleMethod> KernelUpsamplerMethod(const std::string& name) {
    for (const auto& entry : kKernelNames) {
        if (name == entry.first) {
            return entry.second;
        }
    }
    return std::nullopt;
}

void RegisterUpsampler(const std::string& name, UpsamplerFactory factory) {
    if (name.empty()) {
        throw std::invalid_argument("An upsampler type needs a name");
    }
    if (KernelUpsamplerMethod(name) || name == "diamond_square") {
        throw std::invalid_argument("\"" + name + "\" is a built-in upsampler");
    }
    if (!factory) {
        throw std::invalid_argument("An upsampler type needs a factory");
    }
    Registry& registry = UpsamplerRegistry();
    const std::lock_guard lock(registry.mutex);
    registry.factories[name] = std::move(factory);
}

bool IsUpsamplerRegistered(const std::string& name) {
    Registry& registry = UpsamplerRegistry();
    const std::lock_guard lock(registry.mutex);
    return registry.factories.count(name) > 0;
}

std::vector<std::string> RegisteredUpsamplerNames() {
    Registry& registry = UpsamplerRegistry();
    const std::lock_guard lock(registry.mutex);
    std::vector<std::string> names;
    for (const auto& entry : registry.factories) {
        names.push_back(entry.first);
    }
    return names;
}

std::shared_ptr<const Upsampler> MakeRegisteredUpsampler(const std::string& name,
                                                         const std::string& settings) {
    UpsamplerFactory factory;
    {
        Registry& registry = UpsamplerRegistry();
        const std::lock_guard lock(registry.mutex);
        const auto found = registry.factories.find(name);
        if (found == registry.factories.end()) {
            throw std::invalid_argument("No upsampler type is registered as \"" + name + "\"");
        }
        factory = found->second;
    }
    std::shared_ptr<const Upsampler> upsampler = factory(settings);
    if (!upsampler) {
        throw std::invalid_argument("The upsampler type \"" + name + "\" made no upsampler");
    }
    return upsampler;
}

}  // namespace terraingen
