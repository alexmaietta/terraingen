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

void ValidateSyntheticTerrain(const SyntheticTerrainParams& params) {
    ValidateNoiseLayers(params.layers, 1);
}

SyntheticSource::NoisePosts::NoisePosts(const SyntheticSource& source, double pixelSize)
    : source_(source),
      transform_(GeoTransform::NorthUp(-0.5 * pixelSize, 0.5 * pixelSize, pixelSize)) {}

void SyntheticSource::NoisePosts::Read(std::int64_t col0, std::int64_t row0, std::size_t width,
                                       std::size_t height, Span<float> heights) const {
    for (std::size_t r = 0; r < height; ++r) {
        const double row = static_cast<double>(row0 + static_cast<std::int64_t>(r)) + 0.5;
        for (std::size_t c = 0; c < width; ++c) {
            const double col = static_cast<double>(col0 + static_cast<std::int64_t>(c)) + 0.5;
            const Vec2d at = transform_.PixelToWorld(col, row);
            heights[r * width + c] = source_.NoiseAt(at.x, at.y);
        }
    }
}

SyntheticSource::SyntheticSource(SyntheticTerrainParams params, float elevation,
                                 std::optional<double> pixelSize,
                                 std::shared_ptr<const Upsampler> upsampler, Surface surface)
    : params_(std::move(params)), elevation_(elevation), pixelSize_(pixelSize), surface_(surface) {
    ValidateNoiseLayers(params_.layers);
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
    return elevation_ + NoiseLayersAt(params_.layers, {}, surface_, x, y);
}

RasterGrid SyntheticSource::Grid() const {
    return {0, 0, posts_ ? posts_->Transform() : GeoTransform::NorthUp(0.0, 0.0, 1.0)};
}

void SyntheticSource::Fill(Raster<float>& heights, double spacing) const {
    if (posts_) {
        FillUpsampled(*upsampler_, *posts_, heights, spacing);
    } else {
        ParallelFor(heights.Height(), 0, [&](std::size_t row) {
            for (std::size_t col = 0; col < heights.Width(); ++col) {
                const Vec2d center = heights.CellCenter(col, row);
                heights(col, row) = NoiseAt(center.x, center.y);
            }
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
