// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/raster_source.h"

#include "terraingen/util/bits.h"
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

/// `value` clamped to [0, `limit`] as an index.
std::size_t ClampTo(double value, std::size_t limit) {
    return static_cast<std::size_t>(std::clamp(value, 0.0, static_cast<double>(limit)));
}

/// The heights over the whole of a raster.
detail::RasterHeights WholeHeights(Raster<float> heights, const RasterSourceOptions& options,
                                   const GeoKeys& keys) {
    if (heights.Empty()) {
        throw std::invalid_argument("A raster source needs a raster with cells");
    }
    detail::ValidateRasterSourceOptions(options);
    const RasterPlacement placement = RasterPlacement::Whole(heights);
    const bool wraps = detail::WrapsLongitude(keys, placement.whole);
    const std::optional<float> noData = heights.NoData();
    return detail::RasterHeights(
        std::make_shared<RasterPosts>(std::make_shared<const Raster<float>>(std::move(heights)),
                                      placement, wraps),
        placement.whole, noData, options, WholeWindow(placement.whole), "the raster", wraps);
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
      upsampler_(FittedUpsampler(options.upsampler, *posts_)), name_(std::move(name)),
      wrapLongitude_(wrapLongitude) {
    cellSpacing_ = PostSpacing(lattice_.transform);
    resolution_ = options.effectivePixelSize.value_or(cellSpacing_);
}

bool RasterHeights::Covers(double col, double row) const {
    return col >= static_cast<double>(cells_.colBegin) &&
           row >= static_cast<double>(cells_.rowBegin) &&
           col < static_cast<double>(cells_.colEnd) && row < static_cast<double>(cells_.rowEnd);
}

void RasterHeights::FillUnwrapped(Raster<float>& heights, double spacing, unsigned threads) const {
    const GeoTransform& target = heights.Transform();
    if (target.IsRotated() && !(AtPostSpacing(lattice_.transform, spacing) &&
                                detail::OffsetOnLattice(lattice_.transform, target))) {
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
    float height = 0.0f;
    const Vec2d point{x, y};
    HeightsAt(Span<const Vec2d>(&point, 1), Span<float>(&height, 1), spacing);
    return height;
}

void RasterHeights::HeightsAt(Span<const Vec2d> points, Span<float> heights, double spacing) const {
    RequireHeightPerPoint(points, heights);
    std::vector<Vec2d> covered;
    std::vector<std::size_t> at;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const double x = points[i].x - Turns(points[i].x) * kTurnDegrees;
        const Vec2d pixel = lattice_.transform.WorldToPixel(x, points[i].y);
        heights[i] = std::numeric_limits<float>::quiet_NaN();
        if (Covers(pixel.x, pixel.y)) {
            covered.push_back({x, points[i].y});
            at.push_back(i);
        }
    }
    std::vector<float> values(covered.size());
    UpsampledHeights(*upsampler_, *posts_, Span<const Vec2d>(covered), Span<float>(values),
                     spacing);
    for (std::size_t k = 0; k < at.size(); ++k) {
        heights[at[k]] = values[k];
    }
}

bool RasterHeights::HasHeight(double x, double y) const {
    return !std::isnan(HeightAt(x, y, cellSpacing_));
}

double RasterHeights::ResolutionAt(double x, double y) const {
    return HasHeight(x, y) ? resolution_ : std::numeric_limits<double>::infinity();
}

double RasterHeights::PostSpacingAt(double x, double y) const {
    return HasHeight(x, y) ? cellSpacing_ : std::numeric_limits<double>::infinity();
}

}  // namespace detail

RasterSource::RasterSource(Raster<float> heights, const RasterSourceOptions& options, GeoKeys keys)
    : keys_(std::move(keys)), heights_(WholeHeights(std::move(heights), options, keys_)) {}

}  // namespace terraingen
