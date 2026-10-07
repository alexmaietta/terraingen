#include "terraingen/layers/bedforms.h"

#include "terraingen/noise/kernels/perlin.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace terraingen {

namespace {

constexpr std::uint32_t kBedformStream = 0x42454446u;
constexpr double kRadiusMin = 0.40;
constexpr double kRadiusMax = 0.70;
constexpr double kRagged = 0.30;
constexpr double kEdgeShare = 0.35;
constexpr double kWavelengthMin = 0.8;
constexpr double kWavelengthSpan = 0.4;
constexpr double kWanderWavelengths = 5.0;
constexpr double kRagRadii = 0.5;

double Smoothstep(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

}  // namespace

void ValidateBedformOptions(const BedformOptions& options) {
    if (!(std::isfinite(options.wavelength) && options.wavelength > 0.0 &&
          std::isfinite(options.fieldSize) && options.fieldSize > 0.0)) {
        throw std::invalid_argument("Bedform wavelength and field size must be positive");
    }
    if (!(std::isfinite(options.height) && options.height >= 0.0)) {
        throw std::invalid_argument("Bedform height must not be negative");
    }
    if (!(options.leeFraction > 0.0 && options.leeFraction < 1.0)) {
        throw std::invalid_argument("Bedform leeFraction must be in (0, 1)");
    }
    if (!(options.cover >= 0.0 && options.cover <= 0.9)) {
        throw std::invalid_argument("Bedform cover must be in [0, 0.9]");
    }
    if (!(std::isfinite(options.sinuosity) && options.sinuosity >= 0.0 &&
          std::isfinite(options.azimuthSpread) && options.azimuthSpread >= 0.0 &&
          std::isfinite(options.windAzimuth))) {
        throw std::invalid_argument(
            "Bedform sinuosity and azimuth spread must be finite and not negative");
    }
    if (!(std::isfinite(options.exposureSpacing) && options.exposureSpacing > 0.0)) {
        throw std::invalid_argument("Bedform exposureSpacing must be positive");
    }
    if (options.exposure) {
        ValidateExposureParams(*options.exposure);
    }
    ValidateResolutionFade(options.fade);
}

BedformLayer::BedformLayer(BedformOptions options) : options_(options) {
    ValidateBedformOptions(options_);
    cell_ = options_.fieldSize / (kRadiusMin + kRadiusMax);
    const double meanArea = std::numbers::pi *
                            (std::pow(kRadiusMax, 3.0) - std::pow(kRadiusMin, 3.0)) /
                            (3.0 * (kRadiusMax - kRadiusMin));
    density_ = std::min(1.0, -std::log1p(-options_.cover) / meanArea);
    if (options_.exposure) {
        exposure_.emplace(*options_.exposure);
    }
}

BedformLayer::Point BedformLayer::Evaluate(double x, double y, const Ground* ground) const {
    Point point;
    if (density_ <= 0.0) {
        return point;
    }
    if (exposure_ && ground == nullptr) {
        throw std::invalid_argument("Bedforms that read the exposure field need the ground");
    }
    const auto ix = static_cast<std::int64_t>(std::floor(x / cell_));
    const auto iy = static_cast<std::int64_t>(std::floor(y / cell_));
    for (std::int64_t gy = iy - 1; gy <= iy + 1; ++gy) {
        for (std::int64_t gx = ix - 1; gx <= ix + 1; ++gx) {
            RandomStream random(options_.seed, kBedformStream, static_cast<std::uint32_t>(gx),
                                static_cast<std::uint32_t>(gy),
                                static_cast<std::uint32_t>((gx >> 32) ^ ((gy >> 32) << 16)));
            if (random.NextDouble() >= density_) {
                continue;
            }
            const double cx = (static_cast<double>(gx) + random.NextDouble()) * cell_;
            const double cy = (static_cast<double>(gy) + random.NextDouble()) * cell_;
            const double barren = random.NextDouble();
            const auto ox = static_cast<float>(97.0 * random.NextDouble());
            const auto oy = static_cast<float>(97.0 * random.NextDouble());
            const double radius =
                (kRadiusMin + (kRadiusMax - kRadiusMin) * random.NextDouble()) * cell_;
            const double scale = kWavelengthMin + kWavelengthSpan * random.NextDouble();
            const double azimuth =
                options_.windAzimuth + options_.azimuthSpread * (2.0 * random.NextDouble() - 1.0);
            const double phase = random.NextDouble();
            const double dx = x - cx;
            const double dy = y - cy;
            const double rag = static_cast<double>(
                PerlinNoise2(static_cast<float>(dx / (kRagRadii * radius)) + ox,
                             static_cast<float>(dy / (kRagRadii * radius)) + oy, options_.seed));
            const double inside = 1.0 - std::hypot(dx, dy) / (radius * (1.0 + kRagged * rag));
            if (inside <= 0.0) {
                continue;
            }
            if (exposure_ &&
                barren < exposure_->Exposure(*ground, cx, cy, options_.exposureSpacing)) {
                continue;
            }
            const double cover = Smoothstep(inside / kEdgeShare);
            const double wavelength = options_.wavelength * scale;
            const double wander = static_cast<double>(
                PerlinNoise2(static_cast<float>(dx / (kWanderWavelengths * wavelength)) + oy,
                             static_cast<float>(dy / (kWanderWavelengths * wavelength)) + ox,
                             options_.seed + 1u));
            const double along = (dx * std::cos(azimuth) + dy * std::sin(azimuth)) / wavelength +
                                 options_.sinuosity * wander + phase;
            const double t = along - std::floor(along);
            const double upwind = 1.0 - options_.leeFraction;
            const double profile =
                t < upwind
                    ? 0.5 * (1.0 - std::cos(std::numbers::pi * t / upwind))
                    : 0.5 *
                          (1.0 + std::cos(std::numbers::pi * (t - upwind) / options_.leeFraction));
            point.cover += cover;
            point.relief += cover * options_.height * scale * profile;
        }
    }
    return point;
}

double BedformLayer::CoverAt(double x, double y, const Ground* ground) const {
    return std::min(1.0, Evaluate(x, y, ground).cover);
}

float BedformLayer::Add(float height, double x, double y, double spacing,
                        const Ground* ground) const {
    const double weight = ResolutionWeight(options_.wavelength, spacing, options_.fade);
    if (weight <= 0.0) {
        return height;
    }
    const Point point = Evaluate(x, y, ground);
    if (point.cover <= 0.0) {
        return height;
    }
    return static_cast<float>(static_cast<double>(height) +
                              weight * point.relief / std::max(1.0, point.cover));
}

void BedformLayer::Apply(Raster<float>& heights, const LayerContext& context,
                         TerrainFeatures& /*features*/) const {
    if (exposure_ && context.ground == nullptr) {
        throw std::invalid_argument("Bedforms that read the exposure field need the ground");
    }
    const double spacing = std::max(context.spacing, RasterGrid::Of(heights).PixelSize());
    ParallelFor(heights.Height(), context.threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            float& cell = heights(col, row);
            if (!heights.IsNoData(cell)) {
                const Vec2d centre = heights.CellCenter(col, row);
                cell = Add(cell, centre.x, centre.y, spacing, context.ground);
            }
        }
    });
}

float BedformLayer::ApplyAt(float height, double x, double y, const LayerContext& context) const {
    if (!(std::isfinite(context.spacing) && context.spacing > 0.0)) {
        throw std::invalid_argument("Sample spacing must be positive and finite");
    }
    if (std::isnan(height)) {
        return height;
    }
    return Add(height, x, y, context.spacing, context.ground);
}

}  // namespace terraingen
