#include "terraingen/layers/bedforms.h"

#include "terraingen/noise/perlin.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace terraingen {

namespace {

constexpr std::uint32_t kBedformStream = 0x42454446u;
constexpr std::uint32_t kBedformSphereStream = 0x42454453u;
constexpr double kRadiusMin = 0.40;
constexpr double kRadiusMax = 0.70;
constexpr double kRagged = 0.30;
constexpr double kEdgeShare = 0.35;
constexpr double kWavelengthMin = 0.8;
constexpr double kWavelengthSpan = 0.4;
constexpr double kWanderWavelengths = 5.0;
constexpr double kRagRadii = 0.5;

constexpr const char* kExposureReader = "Bedforms that read the exposure field";

}  // namespace

void ValidateBedformOptions(const BedformOptions& options) {
    if (!(IsPositive(options.wavelength) && IsPositive(options.fieldSize))) {
        throw std::invalid_argument("Bedform wavelength and field size must be positive");
    }
    if (!IsNonNegative(options.height)) {
        throw std::invalid_argument("Bedform height must not be negative");
    }
    if (!(options.leeFraction > 0.0 && options.leeFraction < 1.0)) {
        throw std::invalid_argument("Bedform leeFraction must be in (0, 1)");
    }
    if (!(options.cover >= 0.0 && options.cover <= 0.9)) {
        throw std::invalid_argument("Bedform cover must be in [0, 0.9]");
    }
    if (!(IsNonNegative(options.sinuosity) && IsNonNegative(options.azimuthSpread) &&
          std::isfinite(options.windAzimuth))) {
        throw std::invalid_argument(
            "Bedform sinuosity and azimuth spread must be finite and not negative");
    }
    if (!IsPositive(options.exposureSpacing)) {
        throw std::invalid_argument("Bedform exposureSpacing must be positive");
    }
    if (options.exposure) {
        ValidateExposureParams(*options.exposure);
    }
    ValidateResolutionFade(options.fade);
}

BedformOptions MarsLargeRipples() {
    BedformOptions ripples;
    ripples.wavelength = 2.0;
    ripples.height = 0.2;
    const double leeSlope = std::tan(30.0 * std::numbers::pi / 180.0);
    ripples.leeFraction = std::numbers::pi * ripples.height / (2.0 * ripples.wavelength * leeSlope);
    ripples.sinuosity = 0.6;
    ripples.cover = 0.15;
    ripples.fieldSize = 80.0;
    return ripples;
}

BedformOptions MarsTransverseAeolianRidges() {
    BedformOptions ridges;
    ridges.wavelength = 25.8;
    ridges.height = 1.3;
    ridges.leeFraction = 0.5;
    ridges.sinuosity = 0.2;
    ridges.cover = 0.07;
    ridges.fieldSize = 250.0;
    return ridges;
}

BedformLayer::BedformLayer(BedformOptions options, Surface surface)
    : FormulaLayer(surface), options_(options) {
    ValidateBedformOptions(options_);
    cell_ = options_.fieldSize / (kRadiusMin + kRadiusMax);
    const double meanArea = std::numbers::pi *
                            (std::pow(kRadiusMax, 3.0) - std::pow(kRadiusMin, 3.0)) /
                            (3.0 * (kRadiusMax - kRadiusMin));
    density_ = std::min(1.0, -std::log1p(-options_.cover) / meanArea);
    if (options_.exposure) {
        exposure_.emplace(*options_.exposure, surface_);
    }
    if (surface_.IsBody()) {
        cells_.emplace(surface_);
    }
}

void BedformLayer::AddField(Point& point, RandomStream& random, const Vec2d& centre,
                            const Vec2d& offset, const Ground* ground) const {
    const double barren = random.NextDouble();
    const auto ox = static_cast<float>(97.0 * random.NextDouble());
    const auto oy = static_cast<float>(97.0 * random.NextDouble());
    const double radius = (kRadiusMin + (kRadiusMax - kRadiusMin) * random.NextDouble()) * cell_;
    const double scale = kWavelengthMin + kWavelengthSpan * random.NextDouble();
    const double azimuth =
        options_.windAzimuth + options_.azimuthSpread * (2.0 * random.NextDouble() - 1.0);
    const double phase = random.NextDouble();
    const double dx = offset.x;
    const double dy = offset.y;
    const double rag = static_cast<double>(
        PerlinNoise2(static_cast<float>(dx / (kRagRadii * radius)) + ox,
                     static_cast<float>(dy / (kRagRadii * radius)) + oy, options_.seed));
    const double inside = 1.0 - std::hypot(dx, dy) / (radius * (1.0 + kRagged * rag));
    if (inside <= 0.0) {
        return;
    }
    if (exposure_ &&
        barren < exposure_->Exposure(*ground, centre.x, centre.y, options_.exposureSpacing)) {
        return;
    }
    const double cover = Smoothstep(inside / kEdgeShare);
    const double wavelength = options_.wavelength * scale;
    const double wander = static_cast<double>(PerlinNoise2(
        static_cast<float>(dx / (kWanderWavelengths * wavelength)) + oy,
        static_cast<float>(dy / (kWanderWavelengths * wavelength)) + ox, options_.seed + 1u));
    const double along = (dx * std::cos(azimuth) + dy * std::sin(azimuth)) / wavelength +
                         options_.sinuosity * wander + phase;
    const double t = along - std::floor(along);
    const double upwind = 1.0 - options_.leeFraction;
    const double profile =
        t < upwind ? 0.5 * (1.0 - std::cos(std::numbers::pi * t / upwind))
                   : 0.5 * (1.0 + std::cos(std::numbers::pi * (t - upwind) / options_.leeFraction));
    point.cover += cover;
    point.relief += cover * options_.height * scale * profile;
}

BedformLayer::Point BedformLayer::Evaluate(double x, double y, const Ground* ground) const {
    Point point;
    if (density_ <= 0.0) {
        return point;
    }
    if (exposure_) {
        RequireGround(ground, kExposureReader);
    }
    if (cells_) {
        const CellIndex home = cells_->CellOf(x, y, cell_);
        for (const CellIndex& cell : cells_->Neighbours(home, cell_, 2)) {
            RandomStream random(options_.seed, kBedformSphereStream,
                                static_cast<std::uint32_t>(cell.i),
                                static_cast<std::uint32_t>(cell.j));
            const double chance = density_ * cells_->CellArea(cell, cell_) / (cell_ * cell_);
            if (random.NextDouble() >= chance) {
                continue;
            }
            const double u = random.NextDouble();
            const double v = random.NextDouble();
            const Vec2d centre = cells_->SamplePoint(u, v, cell, cell_);
            AddField(point, random, centre, surface_.Offset(centre, {x, y}), ground);
        }
        return point;
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
            AddField(point, random, {cx, cy}, {x - cx, y - cy}, ground);
        }
    }
    return point;
}

double BedformLayer::CoverAt(double x, double y, const Ground* ground) const {
    return std::min(1.0, Evaluate(x, y, ground).cover);
}

float BedformLayer::HeightAt(float height, double x, double y, double spacing,
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

void BedformLayer::CheckContext(const LayerContext& context) const {
    if (exposure_) {
        RequireGround(context.ground, kExposureReader);
    }
}

}  // namespace terraingen
