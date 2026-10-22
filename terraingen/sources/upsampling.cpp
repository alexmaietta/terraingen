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

/// A block of posts read at once, and where it starts in the lattice.
struct PostBlock {
    std::int64_t col0 = 0;
    std::int64_t row0 = 0;
    std::size_t width = 0;
    std::vector<float> values;

    float At(double col, double row) const {
        return values[static_cast<std::size_t>(static_cast<std::int64_t>(row) - row0) * width +
                      static_cast<std::size_t>(static_cast<std::int64_t>(col) - col0)];
    }
};

/// The posts [`col0`, `col1`] x [`row0`, `row1`].
PostBlock ReadBlock(const PostLattice& posts, std::int64_t col0, std::int64_t row0,
                    std::int64_t col1, std::int64_t row1) {
    PostBlock block;
    block.col0 = col0;
    block.row0 = row0;
    block.width = static_cast<std::size_t>(col1 - col0 + 1);
    const auto height = static_cast<std::size_t>(row1 - row0 + 1);
    block.values.resize(block.width * height);
    posts.Read(col0, row0, block.width, height, Span<float>(block.values));
    return block;
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

/// `posts` interpolated with `method` at lattice position (`u`, `v`), read from `block`.
float Interpolate(const PostBlock& block, ResampleMethod method, double u, double v) {
    const std::optional<float> value = InterpolateLattice(
        u, v, method, [&](double col, double row) { return block.At(col, row); },
        [](float post) { return std::isnan(post); });
    return value.value_or(std::numeric_limits<float>::quiet_NaN());
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

RasterPosts::RasterPosts(std::shared_ptr<const Raster<float>> window,
                         const RasterPlacement& placement)
    : window_(std::move(window)), placement_(placement) {
    if (!window_ || window_->Empty()) {
        throw std::invalid_argument("Raster posts need a raster with cells");
    }
}

std::optional<PixelWindow> RasterPosts::Extent() const {
    return PixelWindow{placement_.col, placement_.col + window_->Width(), placement_.row,
                       placement_.row + window_->Height()};
}

void RasterPosts::Read(std::int64_t col0, std::int64_t row0, std::size_t width, std::size_t height,
                       Span<float> heights) const {
    const Raster<float>& window = *window_;
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
            const std::size_t col = index(col0 + static_cast<std::int64_t>(c),
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
                   double spacing) {
    upsampler.Fill(posts, heights, spacing);
    const GeoTransform transform = posts.Transform();
    if (!AtPostSpacing(transform, spacing)) {
        return;
    }
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const Vec2d center = heights.CellCenter(col, row);
            if (const auto post = CenteredPost(transform, center.x, center.y)) {
                posts.Read(post->first, post->second, 1, 1, Span<float>(&heights(col, row), 1));
            }
        }
    }
}

void Upsampler::Fill(const PostLattice& posts, Raster<float>& heights, double spacing) const {
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const Vec2d center = heights.CellCenter(col, row);
            heights(col, row) = HeightAt(posts, center.x, center.y, spacing);
        }
    }
}

float KernelUpsampler::HeightAt(const PostLattice& posts, double x, double y,
                                double /*spacing*/) const {
    const Vec2d pixel = UnrotatedTransform(posts).WorldToPixel(x, y);
    const double u = pixel.x - 0.5;
    const double v = pixel.y - 0.5;
    const auto [col0, col1] = Taps(method_, u);
    const auto [row0, row1] = Taps(method_, v);
    return Interpolate(ReadBlock(posts, col0, row0, col1, row1), method_, u, v);
}

void KernelUpsampler::Fill(const PostLattice& posts, Raster<float>& heights, double spacing) const {
    const GeoTransform transform = UnrotatedTransform(posts);
    const std::size_t blocksX = (heights.Width() + kBlockCells - 1) / kBlockCells;
    const std::size_t blocksY = (heights.Height() + kBlockCells - 1) / kBlockCells;
    const auto taps = static_cast<double>(2 * KernelRadius(method_));
    ParallelFor(blocksX * blocksY, 0, [&](std::size_t block) {
        const std::size_t colBegin = (block % blocksX) * kBlockCells;
        const std::size_t rowBegin = (block / blocksX) * kBlockCells;
        const std::size_t colEnd = std::min(colBegin + kBlockCells, heights.Width());
        const std::size_t rowEnd = std::min(rowBegin + kBlockCells, heights.Height());
        std::vector<Vec2d> lattice;
        lattice.reserve((colEnd - colBegin) * (rowEnd - rowBegin));
        std::int64_t col0 = std::numeric_limits<std::int64_t>::max();
        std::int64_t row0 = std::numeric_limits<std::int64_t>::max();
        std::int64_t col1 = std::numeric_limits<std::int64_t>::min();
        std::int64_t row1 = std::numeric_limits<std::int64_t>::min();
        for (std::size_t row = rowBegin; row < rowEnd; ++row) {
            for (std::size_t col = colBegin; col < colEnd; ++col) {
                const Vec2d center = heights.CellCenter(col, row);
                const Vec2d pixel = transform.WorldToPixel(center.x, center.y);
                const Vec2d at{pixel.x - 0.5, pixel.y - 0.5};
                lattice.push_back(at);
                const auto [first, last] = Taps(method_, at.x);
                const auto [top, bottom] = Taps(method_, at.y);
                col0 = std::min(col0, first);
                col1 = std::max(col1, last);
                row0 = std::min(row0, top);
                row1 = std::max(row1, bottom);
            }
        }
        const double area =
            static_cast<double>(col1 - col0 + 1) * static_cast<double>(row1 - row0 + 1);
        const bool together = area <= 4.0 * taps * taps * static_cast<double>(lattice.size());
        const PostBlock shared = together ? ReadBlock(posts, col0, row0, col1, row1) : PostBlock{};
        std::size_t n = 0;
        for (std::size_t row = rowBegin; row < rowEnd; ++row) {
            for (std::size_t col = colBegin; col < colEnd; ++col, ++n) {
                const Vec2d& at = lattice[n];
                if (together) {
                    heights(col, row) = Interpolate(shared, method_, at.x, at.y);
                    continue;
                }
                const Vec2d center = heights.CellCenter(col, row);
                heights(col, row) = HeightAt(posts, center.x, center.y, spacing);
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
