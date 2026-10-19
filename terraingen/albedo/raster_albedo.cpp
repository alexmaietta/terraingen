#include "terraingen/albedo/raster_albedo.h"

#include "terraingen/albedo/brightness_statistics.h"
#include "terraingen/layers/roughness.h"
#include "terraingen/noise/hash.h"
#include "terraingen/noise/perlin.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

/// Seed of the fixed sample the RMS of `PerlinNoise2` is measured on.
constexpr std::uint32_t kRmsSeed = 0x5eed1234u;
/// Step between the seeds of successive detail octaves (2^32 over the golden ratio).
constexpr std::uint32_t kOctaveSeedStep = 0x9e3779b9u;

/// The lags, in cells, the map's structure function is measured at.
constexpr std::array<std::size_t, 4> kLags{1, 2, 4, 8};

/// The RMS of `PerlinNoise2` over a fixed set of points, so octaves can be given an RMS.
double PerlinRms() {
    static const double rms = [] {
        double sum = 0.0;
        constexpr int kSide = 256;
        for (int j = 0; j < kSide; ++j) {
            for (int i = 0; i < kSide; ++i) {
                const float v =
                    PerlinNoise2(i / 8, j / 8, static_cast<float>(i % 8) / 8.0f + 1.0f / 16.0f,
                                 static_cast<float>(j % 8) / 8.0f + 1.0f / 16.0f, kRmsSeed);
                sum += static_cast<double>(v) * static_cast<double>(v);
            }
        }
        return std::sqrt(sum / (kSide * kSide));
    }();
    return rms;
}

}  // namespace

void ValidateAlbedoDetail(const AlbedoDetail& detail) {
    if (detail.octaves < 1 || detail.octaves > 16) {
        throw std::invalid_argument("Albedo detail needs 1 to 16 octaves");
    }
    RequireNonNegative(detail.scale, "The albedo detail scale");
    if (detail.hurst) {
        RequireUnitInterval(*detail.hurst, "The albedo detail Hurst exponent");
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
    cell_ = RasterGrid::FromRaster(first).Spacing(surface_);
    MeasureRoughness();
    if (options_.detail.hurst) {
        hurst_ = *options_.detail.hurst;
    }
}

void RasterAlbedo::MeasureRoughness() {
    const std::size_t width = bands_.front().Width();
    const std::size_t height = bands_.front().Height();
    const std::vector<double> logs = LogBrightnessGrid(bands_);
    std::vector<double> logLags;
    std::vector<double> logStructure;
    for (const std::size_t lag : kLags) {
        if (const std::optional<double> structure = StructureFunction(logs, width, height, lag)) {
            logLags.push_back(std::log(static_cast<double>(lag)));
            logStructure.push_back(std::log(*structure));
        }
    }
    if (!logLags.empty() && logLags.front() == 0.0) {
        roughness_ = std::sqrt(0.5 * std::exp(logStructure.front()));
    }
    if (logLags.size() >= 2) {
        hurst_ = std::clamp(0.5 * FitSlope(logLags, logStructure), 0.0, 1.0);
    }
}

double RasterAlbedo::DetailSum(double x, double y, double spacing) const {
    const AlbedoDetail& detail = options_.detail;
    const Vec2d origin = bands_.front().CellCenter(0, 0);
    double sum = 0.0;
    for (int octave = 0; octave < detail.octaves; ++octave) {
        const double wavelength = cell_ / std::exp2(octave + 1);
        const double weight = ResolutionWeight(wavelength, spacing, detail.fade);
        if (weight == 0.0) {
            break;
        }
        const std::uint32_t seed =
            PcgHash(detail.seed + kOctaveSeedStep * static_cast<std::uint32_t>(octave));
        float noise = 0.0f;
        if (surface_.IsBody()) {
            const Vec3d p = surface_.Position({x, y});
            noise = PerlinNoise3Precise(p.x / wavelength, p.y / wavelength, p.z / wavelength, seed);
        } else {
            noise =
                PerlinNoise2Precise((x - origin.x) / wavelength, (y - origin.y) / wavelength, seed);
        }
        sum += weight * std::exp2(-(octave + 1) * hurst_) * static_cast<double>(noise);
    }
    return sum;
}

AlbedoSample RasterAlbedo::ValueAt(double x, double y, const AlbedoContext& context) const {
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
    const double sum = DetailSum(x, y, context.spacing);
    const double rms =
        surface_.IsBody() ? std::sqrt(0.5 * Perlin3StructureFunction(1e6)) : PerlinRms();
    const double factor = std::exp(detail.scale * roughness_ * sum / rms);
    for (std::size_t band = 0; band < bands_.size(); ++band) {
        value[band] = static_cast<float>(static_cast<double>(value[band]) * factor);
    }
    return value;
}

}  // namespace terraingen
