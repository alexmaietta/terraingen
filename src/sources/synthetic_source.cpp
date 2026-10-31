// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/synthetic_source.h"

#include "sources/lattice.h"

#include "terraingen/noise/validate.h"
#include "terraingen/random/stream.h"
#include "util/checks.h"

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
    : fractals_(std::move(fractals)), elevation_(elevation), givenPixelSize_(pixelSize),
      pixelSize_(pixelSize ? std::optional<double>(*pixelSize / surface.UnitLength())
                           : std::nullopt),
      surface_(surface), givenUpsampler_(std::move(upsampler)) {
    ValidateFractals(fractals_);
    RequireFinite(static_cast<double>(elevation_), "Elevation");
    if (!pixelSize_) {
        if (givenUpsampler_) {
            throw std::invalid_argument(
                "A synthetic source upsamples only posts, so an upsampler needs a pixel size");
        }
        return;
    }
    RequirePositive(*pixelSize_, "Pixel size");
    posts_.emplace(*this, *pixelSize_);
    upsampler_ = FittedUpsampler(givenUpsampler_, *posts_);
}

std::shared_ptr<const Source> SyntheticSource::Reseeded(std::uint64_t seed) const {
    std::vector<Fractal> fractals = fractals_;
    for (std::size_t n = 0; n < fractals.size(); ++n) {
        fractals[n].seed = ReseedWord(seed, static_cast<std::uint32_t>(n));
    }
    std::shared_ptr<const Upsampler> upsampler = givenUpsampler_;
    if (upsampler) {
        if (auto reseeded = upsampler->Reseeded(
                JoinWords(ReseedWord(seed, kMaxFractals), ReseedWord(seed, kMaxFractals + 1)))) {
            upsampler = std::move(reseeded);
        }
    }
    if (fractals.empty() && upsampler == givenUpsampler_) {
        return nullptr;
    }
    return std::make_shared<SyntheticSource>(std::move(fractals), elevation_, givenPixelSize_,
                                             std::move(upsampler), surface_);
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

void SyntheticSource::FillSamples(SampleBlock& samples, double spacing, unsigned threads) const {
    if (posts_) {
        upsampler_->Fill(*posts_, samples, spacing, threads);
        return;
    }
    detail::FillByTiles(samples, threads, [&](Span<const Vec2d> points, Span<float> heights) {
        NoiseAt(points, heights);
    });
}

double SyntheticSource::PostSpacingAt(double /*x*/, double /*y*/) const {
    return pixelSize_.value_or(std::numeric_limits<double>::infinity());
}

}  // namespace terraingen
