// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/upsampling.h"

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

/// Cells per side of the blocks `KernelUpsampler::Fill` reads posts for at once.
constexpr std::size_t kBlockCells = 64;

/// How close to a post, in posts, a position takes the post's height.
constexpr double kCentered = 1e-9;

/// How far below the post spacing, as a share of it, a spacing still counts as the post spacing.
constexpr double kSpacingTolerance = 1e-9;

/// The post at world (`x`, `y`) of a lattice placed by `transform`, if (`x`, `y`) is one.
std::optional<std::pair<std::int64_t, std::int64_t>> CenteredPost(const GeoTransform& transform,
                                                                  double x, double y) {
    const Vec2d pixel = transform.WorldToPixel(x, y);
    const double col = std::floor(pixel.x);
    const double row = std::floor(pixel.y);
    if (!(std::fabs(pixel.x - col - 0.5) < kCentered &&
          std::fabs(pixel.y - row - 0.5) < kCentered)) {
        return std::nullopt;
    }
    return std::pair{static_cast<std::int64_t>(col), static_cast<std::int64_t>(row)};
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

/// The posts the kernel reads around lattice position `at`.
detail::IndexBox TapBox(ResampleMethod method, const Vec2d& at) {
    const auto [first, last] = Taps(method, at.x);
    const auto [top, bottom] = Taps(method, at.y);
    detail::IndexBox box;
    box.Include(first, last, top, bottom);
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

/// Sets the cells of `tile` of `heights` from `block` at their lattice positions `lattice`, row by
/// row, reusing the weights of the tile's first row along x and of its first column along y in
/// the cells whose positions match them, as on a grid aligned with the posts; to the bits of
/// `Interpolate`.
void FillSeparable(const detail::LatticeWindow<float>& block, ResampleMethod method,
                   const std::vector<Vec2d>& lattice, const PixelWindow& tile,
                   Raster<float>& heights) {
    const std::size_t width = tile.colEnd - tile.colBegin;
    const std::size_t height = tile.rowEnd - tile.rowBegin;
    std::vector<AxisWeights> columns(width);
    for (std::size_t c = 0; c < width; ++c) {
        columns[c] = WeightsAt(method, lattice[c].x);
    }
    const int radius = KernelRadius(method);
    const auto value = [&](double col, double row) {
        return block.At(static_cast<std::int64_t>(col), static_cast<std::int64_t>(row));
    };
    const auto missing = [](float post) { return std::isnan(post); };
    for (std::size_t r = 0; r < height; ++r) {
        const AxisWeights row = WeightsAt(method, lattice[r * width].y);
        for (std::size_t c = 0; c < width; ++c) {
            const Vec2d& at = lattice[r * width + c];
            float& cell = heights(tile.colBegin + c, tile.rowBegin + r);
            if (at.x != columns[c].at || at.y != row.at) {
                cell = Interpolate(block, method, at.x, at.y);
                continue;
            }
            cell = InterpolateLatticeWeights(columns[c].first, row.first, columns[c].weights,
                                             row.weights, radius, value, missing)
                       .value_or(std::numeric_limits<float>::quiet_NaN());
        }
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

/// The kernel upsamplers' names and kernels.
const std::array<std::pair<const char*, ResampleMethod>, 6> kKernelNames{{
    {"nearest", ResampleMethod::Nearest},
    {"bilinear", ResampleMethod::Bilinear},
    {"cubic", ResampleMethod::Cubic},
    {"catmull_rom", ResampleMethod::CatmullRom},
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
    const auto columns = static_cast<std::int64_t>(placement_.whole.width);
    const auto wrapped = [&](std::int64_t at) {
        return wrapColumns_ ? ((at % columns) + columns) % columns : at;
    };
    const auto index = [](std::int64_t at, std::size_t wholeSize, std::size_t first,
                          std::size_t size) {
        const auto last = static_cast<std::int64_t>(wholeSize) - 1;
        const auto whole = static_cast<std::size_t>(std::clamp<std::int64_t>(at, 0, last));
        return whole < first ? std::size_t{0} : std::min(whole - first, size - 1);
    };
    for (std::size_t r = 0; r < height; ++r) {
        const std::size_t row = index(row0 + static_cast<std::int64_t>(r), placement_.whole.height,
                                      placement_.row, window.Height());
        for (std::size_t c = 0; c < width; ++c) {
            const std::size_t col = index(wrapped(col0 + static_cast<std::int64_t>(c)),
                                          placement_.whole.width, placement_.col, window.Width());
            const float value = window(col, row);
            heights[r * width + c] =
                window.IsMissing(value) ? std::numeric_limits<float>::quiet_NaN() : value;
        }
    }
}

double PostSpacing(const GeoTransform& transform) {
    return std::max(std::hypot(transform.pixelWidth, transform.colRotation),
                    std::hypot(transform.rowRotation, transform.pixelHeight));
}

bool AtPostSpacing(const GeoTransform& transform, double spacing) {
    return spacing >= PostSpacing(transform) * (1.0 - kSpacingTolerance);
}

float UpsampledHeight(const Upsampler& upsampler, const PostLattice& posts, double x, double y,
                      double spacing) {
    const GeoTransform transform = posts.Transform();
    if (AtPostSpacing(transform, spacing)) {
        if (const auto post = CenteredPost(transform, x, y)) {
            float height = 0.0f;
            posts.Read(post->first, post->second, 1, 1, Span<float>(&height, 1));
            return height;
        }
    }
    return upsampler.HeightAt(posts, x, y, spacing);
}

void FillUpsampled(const Upsampler& upsampler, const PostLattice& posts, Raster<float>& heights,
                   double spacing, unsigned threads) {
    upsampler.Fill(posts, heights, spacing, threads);
    const GeoTransform transform = posts.Transform();
    if (!AtPostSpacing(transform, spacing)) {
        return;
    }
    ParallelFor(heights.Height(), threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const Vec2d center = heights.CellCenter(col, row);
            if (const auto post = CenteredPost(transform, center.x, center.y)) {
                posts.Read(post->first, post->second, 1, 1, Span<float>(&heights(col, row), 1));
            }
        }
    });
}

void MarkMissing(Raster<float>& heights, std::optional<float> noData) {
    const float fill = noData.value_or(std::numeric_limits<float>::quiet_NaN());
    bool missing = false;
    for (float& value : heights.Values()) {
        if (std::isnan(value)) {
            value = fill;
            missing = true;
        }
    }
    heights.SetNoData(FilledNoData(noData, missing));
}

void Upsampler::Fill(const PostLattice& posts, Raster<float>& heights, double spacing,
                     unsigned threads) const {
    ParallelFor(heights.Height(), threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const Vec2d center = heights.CellCenter(col, row);
            heights(col, row) = HeightAt(posts, center.x, center.y, spacing);
        }
    });
}

float KernelUpsampler::HeightAt(const PostLattice& posts, double x, double y,
                                double /*spacing*/) const {
    const Vec2d pixel = UnrotatedTransform(posts).WorldToPixel(x, y);
    const Vec2d at{pixel.x - 0.5, pixel.y - 0.5};
    return Interpolate(detail::ReadPosts(posts, TapBox(method_, at)), method_, at.x, at.y);
}

void KernelUpsampler::Fill(const PostLattice& posts, Raster<float>& heights, double /*spacing*/,
                           unsigned threads) const {
    const GeoTransform transform = UnrotatedTransform(posts);
    const auto taps = static_cast<double>(2 * KernelRadius(method_));
    ForEachTile(heights.Width(), heights.Height(), kBlockCells, threads,
                [&](const PixelWindow& tile) {
                    std::vector<Vec2d> lattice;
                    detail::IndexBox box;
                    for (std::size_t row = tile.rowBegin; row < tile.rowEnd; ++row) {
                        for (std::size_t col = tile.colBegin; col < tile.colEnd; ++col) {
                            const Vec2d center = heights.CellCenter(col, row);
                            const Vec2d pixel = transform.WorldToPixel(center.x, center.y);
                            lattice.push_back({pixel.x - 0.5, pixel.y - 0.5});
                            const detail::IndexBox read = TapBox(method_, lattice.back());
                            box.Include(read.col0, read.col1, read.row0, read.row1);
                        }
                    }
                    std::optional<detail::LatticeWindow<float>> shared;
                    if (box.Area() <= 4.0 * taps * taps * static_cast<double>(lattice.size())) {
                        shared.emplace(detail::ReadPosts(posts, box));
                    }
                    if (shared && method_ != ResampleMethod::Nearest) {
                        FillSeparable(*shared, method_, lattice, tile, heights);
                        return;
                    }
                    std::size_t n = 0;
                    for (std::size_t row = tile.rowBegin; row < tile.rowEnd; ++row) {
                        for (std::size_t col = tile.colBegin; col < tile.colEnd; ++col, ++n) {
                            const Vec2d& at = lattice[n];
                            heights(col, row) =
                                shared ? Interpolate(*shared, method_, at.x, at.y)
                                       : Interpolate(detail::ReadPosts(posts, TapBox(method_, at)),
                                                     method_, at.x, at.y);
                        }
                    }
                });
}

std::shared_ptr<const Upsampler> DefaultUpsampler() {
    static const std::shared_ptr<const Upsampler> cubic = std::make_shared<KernelUpsampler>();
    return cubic;
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
