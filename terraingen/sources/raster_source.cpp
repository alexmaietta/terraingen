// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/raster_source.h"

#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

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
    return detail::RasterHeights(std::move(heights), placement, options,
                                 WholeWindow(placement.whole), "the raster",
                                 detail::WrapsLongitude(keys, placement.whole));
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

RasterHeights::RasterHeights(std::shared_ptr<const Raster<float>> native,
                             const RasterPlacement& placement, const RasterSourceOptions& options,
                             const PixelWindow& cells, std::string name, bool wrapLongitude)
    : native_(native), nativePlacement_(placement), lattice_(placement.whole), cells_(cells),
      posts_(std::move(native), placement, wrapLongitude), upsampler_(OptionUpsampler(options)),
      name_(std::move(name)), wrapLongitude_(wrapLongitude) {
    if (std::shared_ptr<const Upsampler> fitted = upsampler_->FitTo(posts_)) {
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

void RasterHeights::CopyFromLattice(Raster<float>& heights, LatticeOffset offset) const {
    const Raster<float>& data = *native_;
    const float fill = data.NoData().value_or(std::numeric_limits<float>::quiet_NaN());
    bool outside = false;
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        const long long latticeRow = offset.row + static_cast<long long>(row);
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const long long latticeCol = offset.col + static_cast<long long>(col);
            if (latticeRow < static_cast<long long>(cells_.rowBegin) ||
                latticeCol < static_cast<long long>(cells_.colBegin) ||
                latticeRow >= static_cast<long long>(cells_.rowEnd) ||
                latticeCol >= static_cast<long long>(cells_.colEnd)) {
                heights(col, row) = fill;
                outside = true;
                continue;
            }
            heights(col, row) = data(static_cast<std::size_t>(latticeCol) - nativePlacement_.col,
                                     static_cast<std::size_t>(latticeRow) - nativePlacement_.row);
        }
    }
    heights.SetNoData(FilledNoData(data.NoData(), outside));
}

void RasterHeights::FillUnwrapped(Raster<float>& heights, double spacing) const {
    if (AtPostSpacing(lattice_.transform, spacing)) {
        if (const auto offset = OffsetOnLattice(heights.Transform())) {
            CopyFromLattice(heights, *offset);
            return;
        }
    }
    if (heights.Transform().IsRotated()) {
        throw std::invalid_argument("A rotated grid off the lattice of " + name_ +
                                    " cannot be filled");
    }
    FillUpsampled(*upsampler_, posts_, heights, spacing);
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const Vec2d center = heights.CellCenter(col, row);
            const Vec2d pixel = lattice_.transform.WorldToPixel(center.x, center.y);
            if (!Covers(pixel.x, pixel.y)) {
                heights(col, row) = std::numeric_limits<float>::quiet_NaN();
            }
        }
    }
    MarkMissing(heights, native_->NoData());
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

void RasterHeights::Fill(Raster<float>& heights, double spacing) const {
    const std::size_t width = heights.Width();
    const auto turnsAt = [&](std::size_t col) { return Turns(heights.CellCenter(col, 0).x); };
    if (!wrapLongitude_ || heights.Empty() || heights.Transform().IsRotated() ||
        (turnsAt(0) == 0.0 && turnsAt(width - 1) == 0.0)) {
        FillUnwrapped(heights, spacing);
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
        FillUnwrapped(part, spacing);
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
    return UpsampledHeight(*upsampler_, posts_, wrappedX, y, spacing);
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
