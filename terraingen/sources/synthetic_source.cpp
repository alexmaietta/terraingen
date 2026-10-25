// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/synthetic_source.h"

#include "terraingen/noise/validate.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/parallel.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace terraingen {

SyntheticSource::NoisePosts::NoisePosts(const SyntheticSource& source, double pixelSize)
    : source_(source),
      transform_(GeoTransform::NorthUp(-0.5 * pixelSize, 0.5 * pixelSize, pixelSize)) {}

void SyntheticSource::NoisePosts::Read(std::int64_t col0, std::int64_t row0, std::size_t width,
                                       std::size_t height, Span<float> heights) const {
    std::vector<Vec2d> points(width * height);
    for (std::size_t r = 0; r < height; ++r) {
        const double row = static_cast<double>(row0 + static_cast<std::int64_t>(r)) + 0.5;
        for (std::size_t c = 0; c < width; ++c) {
            const double col = static_cast<double>(col0 + static_cast<std::int64_t>(c)) + 0.5;
            points[r * width + c] = transform_.PixelToWorld(col, row);
        }
    }
    source_.NoiseAt(points, heights);
}

SyntheticSource::SyntheticSource(std::vector<Fractal> fractals, float elevation,
                                 std::optional<double> pixelSize,
                                 std::shared_ptr<const Upsampler> upsampler, Surface surface)
    : fractals_(std::move(fractals)), elevation_(elevation),
      pixelSize_(pixelSize ? std::optional<double>(*pixelSize / surface.UnitLength())
                           : std::nullopt),
      surface_(surface) {
    ValidateFractals(fractals_);
    RequireFinite(static_cast<double>(elevation_), "Elevation");
    if (!pixelSize_) {
        if (upsampler) {
            throw std::invalid_argument(
                "A synthetic source upsamples only posts, so an upsampler needs a pixel size");
        }
        return;
    }
    RequirePositive(*pixelSize_, "Pixel size");
    posts_.emplace(*this, *pixelSize_);
    upsampler_ = upsampler ? std::move(upsampler) : DefaultUpsampler();
    if (std::shared_ptr<const Upsampler> fitted = upsampler_->FitTo(*posts_)) {
        upsampler_ = std::move(fitted);
    }
}

float SyntheticSource::NoiseAt(double x, double y) const {
    return elevation_ + FractalStackAt(fractals_, {}, surface_, x, y);
}

void SyntheticSource::NoiseAt(Span<const Vec2d> points, Span<float> heights) const {
    FractalStackRows(fractals_, {}, surface_, points, heights);
    for (float& height : heights) {
        height = elevation_ + height;
    }
}

RasterGrid SyntheticSource::Grid() const {
    return {0, 0, posts_ ? posts_->Transform() : GeoTransform::NorthUp(0.0, 0.0, 1.0)};
}

void SyntheticSource::Fill(Raster<float>& heights, double spacing, unsigned threads) const {
    if (posts_) {
        FillUpsampled(*upsampler_, *posts_, heights, spacing, threads);
    } else {
        ParallelFor(heights.Height(), threads, [&](std::size_t row) {
            std::vector<Vec2d> points(heights.Width());
            for (std::size_t col = 0; col < points.size(); ++col) {
                points[col] = heights.CellCenter(col, row);
            }
            NoiseAt(points, heights.Values().subspan(row * heights.Width(), heights.Width()));
        });
    }
    MarkMissing(heights, std::nullopt);
}

float SyntheticSource::HeightAt(double x, double y, double spacing) const {
    if (posts_) {
        return UpsampledHeight(*upsampler_, *posts_, x, y, spacing);
    }
    return NoiseAt(x, y);
}

double SyntheticSource::ResolutionAt(double /*x*/, double /*y*/) const {
    return pixelSize_.value_or(std::numeric_limits<double>::infinity());
}

}  // namespace terraingen
