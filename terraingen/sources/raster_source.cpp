#include "terraingen/sources/raster_source.h"

#include "terraingen/raster/gaussian_blur.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

/// Cells read beyond the interpolation's reach on each side of a window: cubic convolution reads
/// two cells either side of a point, and one more absorbs rounding in the cell positions.
constexpr double kInterpolationMargin = 3.0;

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

/// The cells of the Gaussian low-pass's kernel either side of a cell of side `cellSize`, as
/// `GaussianBlur` truncates it.
double BlurRadius(double lowPass, double cellSize) {
    return lowPass > 0.0 ? std::ceil(3.0 * lowPass / std::fabs(cellSize)) : 0.0;
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
    const RasterGrid lattice = detail::SourceLattice(placement.whole, options);
    return detail::RasterHeights(std::move(heights), placement, options, WholeWindow(lattice),
                                 "the raster");
}

}  // namespace

namespace detail {

void ValidateRasterSourceOptions(const RasterSourceOptions& options) {
    if (options.pixelSize) {
        RequirePositive(*options.pixelSize, "Pixel size");
    }
    RequireNonNegative(options.lowPass, "Low-pass deviation");
}

RasterGrid SourceLattice(const RasterGrid& native, const RasterSourceOptions& options) {
    return options.pixelSize ? ResampledGrid(native, *options.pixelSize) : native;
}

PixelWindow NativeWindow(const RasterGrid& native, const RasterSourceOptions& options,
                         const PixelWindow& cells) {
    const GeoTransform& transform = native.transform;
    const double scaleX =
        options.pixelSize ? std::fabs(transform.pixelWidth) / *options.pixelSize : 1.0;
    const double scaleY =
        options.pixelSize ? std::fabs(transform.pixelHeight) / *options.pixelSize : 1.0;
    const double marginX = kInterpolationMargin + BlurRadius(options.lowPass, transform.pixelWidth);
    const double marginY =
        kInterpolationMargin + BlurRadius(options.lowPass, transform.pixelHeight);
    return {
        ClampTo(std::floor(static_cast<double>(cells.colBegin) / scaleX) - marginX, native.width),
        ClampTo(std::ceil(static_cast<double>(cells.colEnd) / scaleX) + marginX, native.width),
        ClampTo(std::floor(static_cast<double>(cells.rowBegin) / scaleY) - marginY, native.height),
        ClampTo(std::ceil(static_cast<double>(cells.rowEnd) / scaleY) + marginY, native.height)};
}

RasterHeights::RasterHeights(std::shared_ptr<const Raster<float>> native,
                             const RasterPlacement& placement, const RasterSourceOptions& options,
                             const PixelWindow& cells, std::string name)
    : nativePlacement_(placement), lattice_(SourceLattice(placement.whole, options)), cells_(cells),
      resampling_(options.resampling), name_(std::move(name)) {
    clipped_ = cells_ != WholeWindow(lattice_);
    const GeoTransform& transform = placement.whole.transform;
    if (options.lowPass > 0.0) {
        native = std::make_shared<const Raster<float>>(
            GaussianBlur(*native, options.lowPass / std::fabs(transform.pixelWidth),
                         options.lowPass / std::fabs(transform.pixelHeight)));
    }
    native_ = std::move(native);
    if (options.pixelSize) {
        heights_ = std::make_shared<const Raster<float>>(
            ResampleWindow(*native_, nativePlacement_, *options.pixelSize, resampling_, cells_));
        heightsPlacement_ = {lattice_, cells_.colBegin, cells_.rowBegin};
    } else {
        heights_ = native_;
        heightsPlacement_ = nativePlacement_;
    }
    resolution_ = std::max(std::hypot(transform.pixelWidth, transform.colRotation),
                           std::hypot(transform.rowRotation, transform.pixelHeight));
    resolution_ = std::max(resolution_, 2.0 * options.lowPass);
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
    const Raster<float>& data = *heights_;
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
            heights(col, row) = data(static_cast<std::size_t>(latticeCol) - heightsPlacement_.col,
                                     static_cast<std::size_t>(latticeRow) - heightsPlacement_.row);
        }
    }
    heights.SetNoData(FilledNoData(data.NoData(), outside));
}

void RasterHeights::Fill(Raster<float>& heights) const {
    if (const auto offset = OffsetOnLattice(heights.Transform())) {
        CopyFromLattice(heights, *offset);
        return;
    }
    if (heights.Transform().IsRotated()) {
        throw std::invalid_argument("A rotated grid off the lattice of " + name_ +
                                    " cannot be filled");
    }
    SampleAtCentres(heights, heights_->NoData(),
                    [this](double x, double y) { return HeightAt(x, y); });
}

float RasterHeights::HeightAt(double x, double y) const {
    constexpr float kMissing = std::numeric_limits<float>::quiet_NaN();
    const Vec2d pixel = lattice_.transform.WorldToPixel(x, y);
    if (clipped_ && !Covers(pixel.x, pixel.y)) {
        return kMissing;
    }
    const double col = std::floor(pixel.x);
    const double row = std::floor(pixel.y);
    const bool centred =
        std::fabs(pixel.x - col - 0.5) < 1e-9 && std::fabs(pixel.y - row - 0.5) < 1e-9;
    if (!centred) {
        return SampleRasterWindow(*native_, nativePlacement_, x, y, resampling_);
    }
    if (!Covers(col, row)) {
        return kMissing;
    }
    const Raster<float>& data = *heights_;
    const float value = data(static_cast<std::size_t>(col) - heightsPlacement_.col,
                             static_cast<std::size_t>(row) - heightsPlacement_.row);
    return data.IsNoData(value) ? kMissing : value;
}

double RasterHeights::ResolutionAt(double x, double y) const {
    return std::isnan(HeightAt(x, y)) ? std::numeric_limits<double>::infinity() : resolution_;
}

}  // namespace detail

RasterSource::RasterSource(Raster<float> heights, RasterSourceOptions options, GeoKeys keys)
    : RasterSource(std::make_shared<const Raster<float>>(std::move(heights)), options,
                   std::move(keys)) {}

RasterSource::RasterSource(std::shared_ptr<const Raster<float>> heights,
                           RasterSourceOptions options, GeoKeys keys)
    : options_(options), keys_(std::move(keys)),
      heights_(WholeHeights(std::move(heights), options)) {}

Vec2d RasterSource::ReferencePoint() const {
    const GeoTransform& transform = heights_.Lattice().transform;
    return {transform.originX, transform.originY};
}

}  // namespace terraingen
