#include "terraingen/sources/raster_source.h"

#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

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
                                   const RasterSourceOptions& options) {
    if (!heights) {
        throw std::invalid_argument("A raster source cannot hold a null raster");
    }
    if (heights->Empty()) {
        throw std::invalid_argument("A raster source needs a raster with cells");
    }
    detail::ValidateRasterSourceOptions(options);
    const RasterPlacement placement = RasterPlacement::Whole(*heights);
    return detail::RasterHeights(std::move(heights), placement, options,
                                 WholeWindow(placement.whole), "the raster");
}

}  // namespace

namespace detail {

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
                             const PixelWindow& cells, std::string name)
    : native_(native), nativePlacement_(placement), lattice_(placement.whole), cells_(cells),
      posts_(std::move(native), placement), upsampler_(OptionUpsampler(options)),
      name_(std::move(name)) {
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

void RasterHeights::Fill(Raster<float>& heights, double spacing) const {
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
    const std::optional<float> noData = native_->NoData();
    const float fill = noData.value_or(std::numeric_limits<float>::quiet_NaN());
    bool missing = false;
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const Vec2d center = heights.CellCenter(col, row);
            const Vec2d pixel = lattice_.transform.WorldToPixel(center.x, center.y);
            float& cell = heights(col, row);
            if (!Covers(pixel.x, pixel.y)) {
                cell = std::numeric_limits<float>::quiet_NaN();
            }
            if (std::isnan(cell)) {
                cell = fill;
                missing = true;
            }
        }
    }
    heights.SetNoData(FilledNoData(noData, missing));
}

float RasterHeights::HeightAt(double x, double y, double spacing) const {
    constexpr float kMissing = std::numeric_limits<float>::quiet_NaN();
    const Vec2d pixel = lattice_.transform.WorldToPixel(x, y);
    if (!Covers(pixel.x, pixel.y)) {
        return kMissing;
    }
    return UpsampledHeight(*upsampler_, posts_, x, y, spacing);
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
      heights_(WholeHeights(std::move(heights), options_)) {}

Vec2d RasterSource::ReferencePoint() const {
    const GeoTransform& transform = heights_.Lattice().transform;
    return {transform.originX, transform.originY};
}

}  // namespace terraingen
