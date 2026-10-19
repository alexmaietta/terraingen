#include "terraingen/sources/field.h"

#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace terraingen {

MinimumField::MinimumField(std::shared_ptr<const SurfaceField> a,
                           std::shared_ptr<const SurfaceField> b)
    : a_(std::move(a)), b_(std::move(b)) {
    if (!a_ || !b_) {
        throw std::invalid_argument("A minimum field cannot take a null field");
    }
}

double MinimumField::ValueAt(double x, double y) const {
    return std::fmin(a_->ValueAt(x, y), b_->ValueAt(x, y));
}

double MinimumField::Max() const {
    return std::fmax(a_->Max(), b_->Max());
}

void ValidateRasterFieldOptions(const RasterFieldOptions& options) {
    if (options.method == ResampleMethod::Cubic) {
        throw std::invalid_argument(
            "A raster field reads nearest or bilinear, which stay within the cells read");
    }
    RequirePositive(options.scale, "Raster field scale");
}

RasterField::RasterField(Raster<float> raster, RasterFieldOptions options)
    : raster_(std::move(raster)), options_(options) {
    ValidateRasterFieldOptions(options_);
    if (raster_.Empty()) {
        throw std::invalid_argument("A raster field needs a raster");
    }
    const GeoTransform& transform = raster_.Transform();
    if (transform.IsRotated()) {
        throw std::invalid_argument("A raster field cannot read a rotated raster");
    }
    for (const float value : raster_.Values()) {
        if (std::isfinite(value) && !raster_.IsNoData(value) &&
            !(static_cast<double>(value) * options_.scale <= max_)) {
            max_ = static_cast<double>(value) * options_.scale;
        }
    }
    if (std::isfinite(options_.fallback) && !(options_.fallback <= max_)) {
        max_ = options_.fallback;
    }
}

double RasterField::Sample(double x, double y) const {
    const Vec2d pixel = raster_.Transform().WorldToPixel(x, y);
    const auto width = static_cast<double>(raster_.Width());
    const auto height = static_cast<double>(raster_.Height());
    if (!(pixel.x >= 0.0 && pixel.y >= 0.0 && pixel.x < width && pixel.y < height)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return options_.method == ResampleMethod::Nearest ? SampleNearest(pixel)
                                                      : SampleBilinear(pixel);
}

bool RasterField::IsValid(std::size_t col, std::size_t row) const {
    const float value = raster_(col, row);
    return std::isfinite(value) && !raster_.IsNoData(value);
}

double RasterField::SampleNearest(const Vec2d& pixel) const {
    const auto col = static_cast<std::size_t>(pixel.x);
    const auto row = static_cast<std::size_t>(pixel.y);
    return IsValid(col, row) ? static_cast<double>(raster_(col, row))
                             : std::numeric_limits<double>::quiet_NaN();
}

double RasterField::SampleBilinear(const Vec2d& pixel) const {
    const auto width = static_cast<double>(raster_.Width());
    const auto height = static_cast<double>(raster_.Height());
    const double u = std::clamp(pixel.x - 0.5, 0.0, width - 1.0);
    const double v = std::clamp(pixel.y - 0.5, 0.0, height - 1.0);
    const auto col = static_cast<std::size_t>(u);
    const auto row = static_cast<std::size_t>(v);
    const double tx = u - static_cast<double>(col);
    const double ty = v - static_cast<double>(row);
    const std::size_t right = std::min(col + 1, raster_.Width() - 1);
    const std::size_t below = std::min(row + 1, raster_.Height() - 1);
    double sum = 0.0;
    double weights = 0.0;
    for (const auto& [c, r, w] :
         {std::tuple{col, row, (1.0 - tx) * (1.0 - ty)}, std::tuple{right, row, tx * (1.0 - ty)},
          std::tuple{col, below, (1.0 - tx) * ty}, std::tuple{right, below, tx * ty}}) {
        if (w > 0.0 && IsValid(c, r)) {
            sum += w * static_cast<double>(raster_(c, r));
            weights += w;
        }
    }
    return weights > 0.0 ? sum / weights : std::numeric_limits<double>::quiet_NaN();
}

double RasterField::ValueAt(double x, double y) const {
    double value = Sample(x, y);
    if (std::isnan(value) && options_.periodicLongitude) {
        value = Sample(x + 360.0, y);
        if (std::isnan(value)) {
            value = Sample(x - 360.0, y);
        }
    }
    if (std::isnan(value)) {
        return options_.fallback;
    }
    return value * options_.scale;
}

std::shared_ptr<const RasterField> ReadRasterField(const std::filesystem::path& path,
                                                   RasterFieldOptions options, std::size_t band) {
    return std::make_shared<RasterField>(ReadGeoTiff(path, band).raster, options);
}

}  // namespace terraingen
