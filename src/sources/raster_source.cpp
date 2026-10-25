// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/raster_source.h"

#include "terraingen/util/arithmetic.h"
#include "terraingen/util/parallel.h"
#include "util/checks.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// A turn of longitude, in degrees.
constexpr double kTurnDegrees = 360.0;

/// `value` as an integer when it is one to within rounding, as offsets between lattices are.
std::optional<long long> Whole(double value) {
    const double rounded = std::round(value);
    if (std::fabs(value - rounded) > 1e-6) {
        return std::nullopt;
    }
    return static_cast<long long>(rounded);
}

/// `value` clamped to [0, `limit`] as an index.
std::size_t ClampTo(double value, std::size_t limit) {
    return static_cast<std::size_t>(std::clamp(value, 0.0, static_cast<double>(limit)));
}

/// The heights over the whole of a raster.
detail::RasterHeights WholeHeights(std::shared_ptr<const Raster<float>> heights,
                                   const RasterSourceOptions& options, const GeoKeys& keys) {
    if (!heights) {
        throw std::invalid_argument("A raster source cannot hold a null raster");
    }
    if (heights->Empty()) {
        throw std::invalid_argument("A raster source needs a raster with cells");
    }
    detail::ValidateRasterSourceOptions(options);
    const RasterPlacement placement = RasterPlacement::Whole(*heights);
    const bool wraps = detail::WrapsLongitude(keys, placement.whole);
    const std::optional<float> noData = heights->NoData();
    return detail::RasterHeights(
        std::make_shared<RasterPosts>(std::move(heights), placement, wraps), placement.whole,
        noData, options, WholeWindow(placement.whole), "the raster", wraps);
}

}  // namespace

namespace detail {

bool WrapsLongitude(const GeoKeys& keys, const RasterGrid& lattice) {
    const GeoTransform& transform = lattice.transform;
    const double span = std::fabs(static_cast<double>(lattice.width) * transform.pixelWidth);
    return keys.IsGeographic() && !transform.IsRotated() &&
           std::fabs(span - kTurnDegrees) <= 1e-9 * kTurnDegrees;
}

void ValidateRasterSourceOptions(const RasterSourceOptions& options) {
    if (options.effectivePixelSize) {
        RequirePositive(*options.effectivePixelSize, "Effective pixel size");
    }
}

std::shared_ptr<const Upsampler> OptionUpsampler(const RasterSourceOptions& options) {
    return options.upsampler ? options.upsampler : DefaultUpsampler();
}

PixelWindow NativeWindow(const RasterGrid& native, int reach, const PixelWindow& cells) {
    const double margin = static_cast<double>(std::max(reach, 0)) + 1.0;
    return {ClampTo(static_cast<double>(cells.colBegin) - margin, native.width),
            ClampTo(static_cast<double>(cells.colEnd) + margin, native.width),
            ClampTo(static_cast<double>(cells.rowBegin) - margin, native.height),
            ClampTo(static_cast<double>(cells.rowEnd) + margin, native.height)};
}

RasterHeights::RasterHeights(std::shared_ptr<const PostLattice> posts, const RasterGrid& lattice,
                             std::optional<float> noData, const RasterSourceOptions& options,
                             const PixelWindow& cells, std::string name, bool wrapLongitude)
    : posts_(std::move(posts)), lattice_(lattice), noData_(noData), cells_(cells),
      upsampler_(OptionUpsampler(options)), name_(std::move(name)), wrapLongitude_(wrapLongitude) {
    if (std::shared_ptr<const Upsampler> fitted = upsampler_->FitTo(*posts_)) {
        upsampler_ = std::move(fitted);
    }
    cellSpacing_ = PostSpacing(lattice_.transform);
    resolution_ = options.effectivePixelSize.value_or(cellSpacing_);
}

std::optional<RasterHeights::LatticeOffset>
RasterHeights::OffsetOnLattice(const GeoTransform& target) const {
    const GeoTransform& source = lattice_.transform;
    const auto same = [](double a, double b) { return std::fabs(a - b) <= 1e-12 * std::fabs(b); };
    const std::optional<long long> col =
        Whole((target.originX - source.originX) / source.pixelWidth);
    const std::optional<long long> row =
        Whole((target.originY - source.originY) / source.pixelHeight);
    if (!same(target.pixelWidth, source.pixelWidth) ||
        !same(target.pixelHeight, source.pixelHeight) || target.rowRotation != source.rowRotation ||
        target.colRotation != source.colRotation || !col || !row) {
        return std::nullopt;
    }
    return LatticeOffset{*col, *row};
}

bool RasterHeights::Covers(double col, double row) const {
    return col >= static_cast<double>(cells_.colBegin) &&
           row >= static_cast<double>(cells_.rowBegin) &&
           col < static_cast<double>(cells_.colEnd) && row < static_cast<double>(cells_.rowEnd);
}

void RasterHeights::CopyFromLattice(Raster<float>& heights, LatticeOffset offset,
                                    unsigned threads) const {
    const float fill = noData_.value_or(std::numeric_limits<float>::quiet_NaN());
    const auto clamp = [](long long at, std::size_t begin, std::size_t end) {
        return std::clamp(at, static_cast<long long>(begin), static_cast<long long>(end));
    };
    const auto width = static_cast<long long>(heights.Width());
    const auto height = static_cast<long long>(heights.Height());
    const long long colBegin = clamp(offset.col, cells_.colBegin, cells_.colEnd);
    const long long colEnd = clamp(offset.col + width, cells_.colBegin, cells_.colEnd);
    const long long rowBegin = clamp(offset.row, cells_.rowBegin, cells_.rowEnd);
    const long long rowEnd = clamp(offset.row + height, cells_.rowBegin, cells_.rowEnd);
    const auto inWidth = static_cast<std::size_t>(colEnd - colBegin);
    const auto inHeight = static_cast<std::size_t>(rowEnd - rowBegin);
    for (float& value : heights.Values()) {
        value = fill;
    }
    if (inWidth > 0 && inHeight > 0) {
        constexpr std::size_t kStripRows = 64;
        ParallelFor(CeilDiv(inHeight, kStripRows), threads, [&](std::size_t strip) {
            const std::size_t first = strip * kStripRows;
            const std::size_t rows = std::min(kStripRows, inHeight - first);
            std::vector<float> posts(inWidth * rows);
            posts_->Read(colBegin, rowBegin + static_cast<long long>(first), inWidth, rows,
                         Span<float>(posts));
            for (std::size_t row = 0; row < rows; ++row) {
                for (std::size_t col = 0; col < inWidth; ++col) {
                    const float value = posts[row * inWidth + col];
                    heights(static_cast<std::size_t>(colBegin - offset.col) + col,
                            static_cast<std::size_t>(rowBegin - offset.row) + first + row) =
                        std::isnan(value) ? fill : value;
                }
            }
        });
    }
    heights.SetNoData(FilledNoData(noData_, inWidth * inHeight < heights.Size()));
}

void RasterHeights::FillUnwrapped(Raster<float>& heights, double spacing, unsigned threads) const {
    if (AtPostSpacing(lattice_.transform, spacing)) {
        if (const auto offset = OffsetOnLattice(heights.Transform())) {
            CopyFromLattice(heights, *offset, threads);
            return;
        }
    }
    if (heights.Transform().IsRotated()) {
        throw std::invalid_argument("A rotated grid off the lattice of " + name_ +
                                    " cannot be filled");
    }
    FillUpsampled(*upsampler_, *posts_, heights, spacing, threads);
    ParallelFor(heights.Height(), threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const Vec2d center = heights.CellCenter(col, row);
            const Vec2d pixel = lattice_.transform.WorldToPixel(center.x, center.y);
            if (!Covers(pixel.x, pixel.y)) {
                heights(col, row) = std::numeric_limits<float>::quiet_NaN();
            }
        }
    });
    MarkMissing(heights, noData_);
}

double RasterHeights::Turns(double x) const {
    if (!wrapLongitude_) {
        return 0.0;
    }
    const GeoTransform& transform = lattice_.transform;
    const double left =
        std::min(transform.originX,
                 transform.originX + static_cast<double>(lattice_.width) * transform.pixelWidth);
    return std::floor((x - left) / kTurnDegrees);
}

void RasterHeights::Fill(Raster<float>& heights, double spacing, unsigned threads) const {
    const std::size_t width = heights.Width();
    const auto turnsAt = [&](std::size_t col) { return Turns(heights.CellCenter(col, 0).x); };
    if (!wrapLongitude_ || heights.Empty() || heights.Transform().IsRotated() ||
        (turnsAt(0) == 0.0 && turnsAt(width - 1) == 0.0)) {
        FillUnwrapped(heights, spacing, threads);
        return;
    }
    std::optional<float> noData;
    for (std::size_t begin = 0; begin < width;) {
        const double turns = turnsAt(begin);
        std::size_t end = begin + 1;
        while (end < width && turnsAt(end) == turns) {
            ++end;
        }
        GeoTransform transform = heights.Transform();
        transform.originX +=
            static_cast<double>(begin) * transform.pixelWidth - turns * kTurnDegrees;
        Raster<float> part(end - begin, heights.Height());
        part.SetTransform(transform);
        FillUnwrapped(part, spacing, threads);
        for (std::size_t row = 0; row < part.Height(); ++row) {
            for (std::size_t col = 0; col < part.Width(); ++col) {
                heights(begin + col, row) = part(col, row);
            }
        }
        noData = noData ? noData : part.NoData();
        begin = end;
    }
    heights.SetNoData(noData);
}

float RasterHeights::HeightAt(double x, double y, double spacing) const {
    constexpr float kMissing = std::numeric_limits<float>::quiet_NaN();
    const double wrappedX = x - Turns(x) * kTurnDegrees;
    const Vec2d pixel = lattice_.transform.WorldToPixel(wrappedX, y);
    if (!Covers(pixel.x, pixel.y)) {
        return kMissing;
    }
    return UpsampledHeight(*upsampler_, *posts_, wrappedX, y, spacing);
}

double RasterHeights::ResolutionAt(double x, double y) const {
    return std::isnan(HeightAt(x, y, cellSpacing_)) ? std::numeric_limits<double>::infinity()
                                                    : resolution_;
}

}  // namespace detail

RasterSource::RasterSource(Raster<float> heights, RasterSourceOptions options, GeoKeys keys)
    : RasterSource(std::make_shared<const Raster<float>>(std::move(heights)), std::move(options),
                   std::move(keys)) {}

RasterSource::RasterSource(std::shared_ptr<const Raster<float>> heights,
                           RasterSourceOptions options, GeoKeys keys)
    : options_(std::move(options)), keys_(std::move(keys)),
      heights_(WholeHeights(std::move(heights), options_, keys_)) {}

}  // namespace terraingen
