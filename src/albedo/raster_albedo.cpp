// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/albedo/raster_albedo.h"

#include "terraingen/albedo/albedo_fields.h"
#include "terraingen/analysis/variogram.h"
#include "terraingen/noise/hash.h"
#include "terraingen/noise/perlin.h"
#include "terraingen/noise/structure_function.h"
#include "terraingen/random/streams.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

/// Step between the seeds of successive detail octaves (2^32 over the golden ratio).
constexpr std::uint32_t kOctaveSeedStep = 0x9e3779b9u;

/// The logarithm of the mean of the bands at each cell, NaN where a band has no data or is not
/// positive.
Raster<float> LogBrightness(const std::vector<Raster<float>>& bands) {
    Raster<float> logs(bands.front().Width(), bands.front().Height());
    for (std::size_t row = 0; row < logs.Height(); ++row) {
        for (std::size_t col = 0; col < logs.Width(); ++col) {
            double sum = 0.0;
            for (const Raster<float>& band : bands) {
                const float value = band(col, row);
                sum += band.IsNoData(value) || !std::isfinite(value) || value <= 0.0f
                           ? std::numeric_limits<double>::quiet_NaN()
                           : static_cast<double>(value);
            }
            logs(col, row) = static_cast<float>(std::log(sum / static_cast<double>(bands.size())));
        }
    }
    return logs;
}

/// The RMS of `PerlinNoise2` over a fixed set of points, so octaves can be given an RMS.
double PerlinRms() {
    static const double rms = [] {
        double sum = 0.0;
        constexpr int kSide = 256;
        for (int j = 0; j < kSide; ++j) {
            for (int i = 0; i < kSide; ++i) {
                const float v = PerlinNoise2(
                    i / 8, j / 8, static_cast<float>(i % 8) / 8.0f + 1.0f / 16.0f,
                    static_cast<float>(j % 8) / 8.0f + 1.0f / 16.0f, kAlbedoDetailRmsStream);
                sum += static_cast<double>(v) * static_cast<double>(v);
            }
        }
        return std::sqrt(sum / (kSide * kSide));
    }();
    return rms;
}

}  // namespace

void ValidateAlbedoDetail(const AlbedoDetail& detail) {
    CheckRanges(detail);
    ValidateResolutionFade(detail.fade);
}

RasterAlbedo::RasterAlbedo(std::vector<Raster<float>> bands, RasterAlbedoOptions options)
    : bands_(std::move(bands)), options_(options) {
    if (bands_.size() != 1 && bands_.size() != kMaxAlbedoBands) {
        throw std::invalid_argument("An albedo map must have 1 or 3 bands");
    }
    const Raster<float>& first = bands_.front();
    if (first.Empty() || first.Transform().IsRotated()) {
        throw std::invalid_argument("An albedo map must be neither empty nor rotated");
    }
    for (const Raster<float>& band : bands_) {
        if (band.Width() != first.Width() || band.Height() != first.Height() ||
            !(band.Transform() == first.Transform())) {
            throw std::invalid_argument("The bands of an albedo map must share one grid");
        }
    }
    ValidateAlbedoDetail(options_.detail);
    MeasureRoughness();
    if (options_.detail.hurst) {
        hurst_ = *options_.detail.hurst;
    }
}

void RasterAlbedo::MeasureRoughness() {
    const Raster<float> logs = LogBrightness(bands_);
    const DeviogramFit fit = FitDeviogram(logs);
    roughness_ = fit.cellRms / std::sqrt(2.0);
    if (fit.lags >= 2) {
        hurst_ = std::clamp(fit.slope, 0.0, 1.0);
    }
}

double RasterAlbedo::DetailSum(double x, double y, double spacing, const Surface& surface) const {
    const AlbedoDetail& detail = options_.detail;
    const Vec2d origin = bands_.front().CellCenter(0, 0);
    const double cell = bands_.front().Grid().Spacing(surface);
    double sum = 0.0;
    for (int octave = 0; octave < detail.octaves; ++octave) {
        const double wavelength = cell / std::exp2(octave + 1);
        const double weight = ResolutionWeight(wavelength, spacing, detail.fade);
        if (weight == 0.0) {
            break;
        }
        const std::uint32_t seed =
            PcgHash(detail.seed + kOctaveSeedStep * static_cast<std::uint32_t>(octave));
        float noise = 0.0f;
        if (surface.IsBody()) {
            const Vec3d p = surface.Position({x, y});
            noise = PerlinNoise3Precise(p.x / wavelength, p.y / wavelength, p.z / wavelength, seed);
        } else {
            noise =
                PerlinNoise2Precise((x - origin.x) / wavelength, (y - origin.y) / wavelength, seed);
        }
        sum += weight * std::exp2(-(octave + 1) * hurst_) * static_cast<double>(noise);
    }
    return sum;
}

AlbedoSample RasterAlbedo::ValueAt(double x, double y, const LayerContext& context) const {
    AlbedoSample value{};
    for (std::size_t band = 0; band < bands_.size(); ++band) {
        value[band] = SampleRaster(bands_[band], x, y, options_.method);
        if (std::isnan(value[band])) {
            value.fill(std::numeric_limits<float>::quiet_NaN());
            return value;
        }
    }
    const AlbedoDetail& detail = options_.detail;
    if (!detail.enabled || detail.scale == 0.0 || roughness_ == 0.0) {
        return value;
    }
    const Surface& surface = context.SurfaceOrPlane();
    const double sum = DetailSum(x, y, context.spacing, surface);
    const double rms =
        surface.IsBody() ? std::sqrt(0.5 * Perlin3StructureFunction(1e6)) : PerlinRms();
    const double factor = std::exp(detail.scale * roughness_ * sum / rms);
    for (std::size_t band = 0; band < bands_.size(); ++band) {
        value[band] = static_cast<float>(static_cast<double>(value[band]) * factor);
    }
    return value;
}

}  // namespace terraingen
