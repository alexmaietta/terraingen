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
    : m_source(source),
      m_transform(GeoTransform::NorthUp(-0.5 * pixelSize, 0.5 * pixelSize, pixelSize)) {}

void SyntheticSource::NoisePosts::Read(std::int64_t col0, std::int64_t row0, std::size_t width,
                                       std::size_t height, Span<float> heights) const {
    std::vector<Vec2d> points(width * height);
    for (std::size_t r = 0; r < height; ++r) {
        const double row = static_cast<double>(row0 + static_cast<std::int64_t>(r)) + 0.5;
        for (std::size_t c = 0; c < width; ++c) {
            const double col = static_cast<double>(col0 + static_cast<std::int64_t>(c)) + 0.5;
            points[r * width + c] = m_transform.PixelToWorld(col, row);
        }
    }
    m_source.NoiseAt(points, heights);
}

SyntheticSource::SyntheticSource(std::vector<Fractal> fractals, float elevation,
                                 std::optional<double> pixelSize,
                                 std::shared_ptr<const Upsampler> upsampler, Surface surface)
    : m_fractals(std::move(fractals)), m_elevation(elevation), m_givenPixelSize(pixelSize),
      m_pixelSize(pixelSize ? std::optional<double>(*pixelSize / surface.UnitLength())
                            : std::nullopt),
      m_surface(surface), m_givenUpsampler(std::move(upsampler)) {
    ValidateFractals(m_fractals);
    RequireFinite(static_cast<double>(m_elevation), "Elevation");
    if (!m_pixelSize) {
        if (m_givenUpsampler) {
            throw std::invalid_argument(
                "A synthetic source upsamples only posts, so an upsampler needs a pixel size");
        }
        return;
    }
    RequirePositive(*m_pixelSize, "Pixel size");
    m_posts.emplace(*this, *m_pixelSize);
    m_upsampler = FittedUpsampler(m_givenUpsampler, *m_posts);
}

std::shared_ptr<const Source> SyntheticSource::Reseeded(std::uint64_t seed) const {
    std::vector<Fractal> fractals = m_fractals;
    for (std::size_t n = 0; n < fractals.size(); ++n) {
        fractals[n].seed = ReseedWord(seed, static_cast<std::uint32_t>(n));
    }
    std::shared_ptr<const Upsampler> upsampler = m_givenUpsampler;
    if (upsampler) {
        if (auto reseeded = upsampler->Reseeded(
                JoinWords(ReseedWord(seed, kMaxFractals), ReseedWord(seed, kMaxFractals + 1)))) {
            upsampler = std::move(reseeded);
        }
    }
    if (fractals.empty() && upsampler == m_givenUpsampler) {
        return nullptr;
    }
    return std::make_shared<SyntheticSource>(std::move(fractals), m_elevation, m_givenPixelSize,
                                             std::move(upsampler), m_surface);
}

void SyntheticSource::NoiseAt(Span<const Vec2d> points, Span<float> heights) const {
    FractalStackRows(m_fractals, {}, m_surface, points, heights);
    for (float& height : heights) {
        height = m_elevation + height;
    }
}

RasterGrid SyntheticSource::Grid() const {
    return {0, 0, m_posts ? m_posts->Transform() : GeoTransform::NorthUp(0.0, 0.0, 1.0)};
}

void SyntheticSource::FillSamples(SampleBlock& samples, double spacing, unsigned threads) const {
    if (m_posts) {
        m_upsampler->Fill(*m_posts, samples, spacing, threads);
        return;
    }
    detail::FillByTiles(samples, threads, [&](Span<const Vec2d> points, Span<float> heights) {
        NoiseAt(points, heights);
    });
}

double SyntheticSource::PostSpacingAt(double /*x*/, double /*y*/) const {
    return m_pixelSize.value_or(std::numeric_limits<double>::infinity());
}

}  // namespace terraingen
