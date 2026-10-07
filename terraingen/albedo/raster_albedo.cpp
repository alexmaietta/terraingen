#include "terraingen/albedo/raster_albedo.h"

#include "terraingen/layers/roughness.h"
#include "terraingen/noise/kernels/hash.h"
#include "terraingen/noise/kernels/perlin.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

/// The RMS of `PerlinNoise2` over a fixed set of points, so octaves can be given an RMS.
double PerlinRms() {
    static const double rms = [] {
        double sum = 0.0;
        constexpr int kSide = 256;
        for (int j = 0; j < kSide; ++j) {
            for (int i = 0; i < kSide; ++i) {
                const float v =
                    PerlinNoise2(i / 8, j / 8, static_cast<float>(i % 8) / 8.0f + 1.0f / 16.0f,
                                 static_cast<float>(j % 8) / 8.0f + 1.0f / 16.0f, 0x5eed1234u);
                sum += static_cast<double>(v) * static_cast<double>(v);
            }
        }
        return std::sqrt(sum / (kSide * kSide));
    }();
    return rms;
}

/// The logarithm of the mean of the bands at a cell, or NaN where it has no data.
double LogBrightness(const std::vector<Raster<float>>& bands, std::size_t col, std::size_t row) {
    double sum = 0.0;
    for (const Raster<float>& band : bands) {
        const float value = band(col, row);
        if (band.IsNoData(value) || !std::isfinite(value) || value <= 0.0f) {
            return NAN;
        }
        sum += static_cast<double>(value);
    }
    return std::log(sum / static_cast<double>(bands.size()));
}

}  // namespace

void ValidateAlbedoDetail(const AlbedoDetail& detail) {
    if (detail.octaves < 1 || detail.octaves > 16) {
        throw std::invalid_argument("Albedo detail needs 1 to 16 octaves");
    }
    if (!(std::isfinite(detail.scale) && detail.scale >= 0.0)) {
        throw std::invalid_argument("The albedo detail scale must not be negative");
    }
    if (detail.hurst && !(*detail.hurst >= 0.0 && *detail.hurst <= 1.0)) {
        throw std::invalid_argument("The albedo detail Hurst exponent must be in [0, 1]");
    }
    ValidateResolutionFade(detail.fade);
}

RasterAlbedo::RasterAlbedo(std::vector<Raster<float>> bands, RasterAlbedoOptions options,
                           Surface surface)
    : bands_(std::move(bands)), options_(options), surface_(surface) {
    if (bands_.size() != 1 && bands_.size() != kMaxAlbedoBands) {
        throw std::invalid_argument("An albedo map must have 1 or 3 bands");
    }
    const Raster<float>& first = bands_.front();
    if (first.Empty() || first.Transform().rowRotation != 0.0 ||
        first.Transform().colRotation != 0.0) {
        throw std::invalid_argument("An albedo map must be neither empty nor rotated");
    }
    for (const Raster<float>& band : bands_) {
        if (band.Width() != first.Width() || band.Height() != first.Height() ||
            !(band.Transform() == first.Transform())) {
            throw std::invalid_argument("The bands of an albedo map must share one grid");
        }
    }
    ValidateAlbedoDetail(options_.detail);
    cell_ = RasterGrid::Of(first).Spacing(surface_);

    const std::size_t width = first.Width();
    const std::size_t height = first.Height();
    std::vector<double> logs(width * height);
    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t col = 0; col < width; ++col) {
            logs[row * width + col] = LogBrightness(bands_, col, row);
        }
    }
    std::vector<double> lags;
    std::vector<double> structure;
    for (const std::size_t lag : std::array<std::size_t, 4>{1, 2, 4, 8}) {
        double sum = 0.0;
        std::size_t pairs = 0;
        const auto add = [&](double a, double b) {
            if (std::isfinite(a) && std::isfinite(b)) {
                sum += (a - b) * (a - b);
                ++pairs;
            }
        };
        for (std::size_t row = 0; row < height; ++row) {
            for (std::size_t col = 0; col < width; ++col) {
                const double here = logs[row * width + col];
                if (col + lag < width) {
                    add(here, logs[row * width + col + lag]);
                }
                if (row + lag < height) {
                    add(here, logs[(row + lag) * width + col]);
                }
            }
        }
        if (pairs > 0 && sum > 0.0) {
            lags.push_back(std::log(static_cast<double>(lag)));
            structure.push_back(std::log(sum / static_cast<double>(pairs)));
        }
    }
    if (!lags.empty() && lags.front() == 0.0) {
        roughness_ = std::sqrt(0.5 * std::exp(structure.front()));
    }
    if (lags.size() >= 2) {
        const auto n = static_cast<double>(lags.size());
        double mx = 0.0;
        double my = 0.0;
        for (std::size_t i = 0; i < lags.size(); ++i) {
            mx += lags[i] / n;
            my += structure[i] / n;
        }
        double sxy = 0.0;
        double sxx = 0.0;
        for (std::size_t i = 0; i < lags.size(); ++i) {
            sxy += (lags[i] - mx) * (structure[i] - my);
            sxx += (lags[i] - mx) * (lags[i] - mx);
        }
        hurst_ = std::clamp(0.5 * sxy / sxx, 0.0, 1.0);
    }
    if (options_.detail.hurst) {
        hurst_ = *options_.detail.hurst;
    }
}

AlbedoSample RasterAlbedo::ValueAt(double x, double y, const AlbedoContext& context) const {
    AlbedoSample value{};
    for (std::size_t band = 0; band < bands_.size(); ++band) {
        value[band] = SampleRaster(bands_[band], x, y, options_.method);
        if (std::isnan(value[band])) {
            value.fill(NAN);
            return value;
        }
    }
    const AlbedoDetail& detail = options_.detail;
    if (!detail.enabled || detail.scale == 0.0 || roughness_ == 0.0) {
        return value;
    }
    const Vec2d origin = bands_.front().CellCenter(0, 0);
    double sum = 0.0;
    for (int octave = 0; octave < detail.octaves; ++octave) {
        const double wavelength = cell_ / std::exp2(octave + 1);
        const double weight = ResolutionWeight(wavelength, context.spacing, detail.fade);
        if (weight == 0.0) {
            break;
        }
        if (surface_.IsSphere()) {
            const Vec3d p = surface_.Position({x, y});
            const float noise = PerlinNoise3Precise(
                p.x / wavelength, p.y / wavelength, p.z / wavelength,
                PcgHash(detail.seed + 0x9e3779b9u * static_cast<std::uint32_t>(octave)));
            sum += weight * std::exp2(-(octave + 1) * hurst_) * static_cast<double>(noise);
            continue;
        }
        const double u = (x - origin.x) / wavelength;
        const double v = (y - origin.y) / wavelength;
        const double iu = std::floor(u);
        const double iv = std::floor(v);
        const float noise =
            PerlinNoise2(static_cast<std::int32_t>(static_cast<std::int64_t>(iu)),
                         static_cast<std::int32_t>(static_cast<std::int64_t>(iv)),
                         static_cast<float>(u - iu), static_cast<float>(v - iv),
                         PcgHash(detail.seed + 0x9e3779b9u * static_cast<std::uint32_t>(octave)));
        sum += weight * std::exp2(-(octave + 1) * hurst_) * static_cast<double>(noise);
    }
    const double rms =
        surface_.IsSphere() ? std::sqrt(0.5 * Perlin3StructureFunction(1e6)) : PerlinRms();
    const double factor = std::exp(detail.scale * roughness_ * sum / rms);
    for (std::size_t band = 0; band < bands_.size(); ++band) {
        value[band] = static_cast<float>(static_cast<double>(value[band]) * factor);
    }
    return value;
}

}  // namespace terraingen
