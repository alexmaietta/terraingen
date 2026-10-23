// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/bedforms.h"

#include "terraingen/layers/lattice.h"
#include "terraingen/layers/source_resolved.h"
#include "terraingen/noise/perlin.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/geometry.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraingen {

namespace {

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
    ValidateSourceResolvesPixels(options.sourceResolvesPixels);
}

BedformOptions MarsLargeRipples() {
    BedformOptions ripples;
    ripples.wavelength = 2.0;
    ripples.height = 0.2;
    const double leeSlope = std::tan(30.0 * kDegree);
    ripples.leeFraction = kPi * ripples.height / (2.0 * ripples.wavelength * leeSlope);
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

BedformLayer::BedformLayer(BedformOptions options, Surface surface,
                           std::shared_ptr<const Source> source)
    : FormulaLayer(surface), options_(options) {
    ValidateBedformOptions(options_);
    limit_ = LayerResolvedLimit(source, options_.sourceResolvesPixels, surface);
    cell_ = options_.fieldSize / (kRadiusMin + kRadiusMax);
    const double meanArea = kPi * (std::pow(kRadiusMax, 3.0) - std::pow(kRadiusMin, 3.0)) /
                            (3.0 * (kRadiusMax - kRadiusMin));
    density_ = std::min(1.0, -std::log1p(-options_.cover) / meanArea);
    wanderSeed_ = SubstreamSeed(options_.seed, kBedformWanderStream);
    exposure_ =
        LayerExposure(options_.exposure, options_.exposureSpacing, surface_, kExposureReader);
    cells_ = MakeCells(surface_);
}

void BedformLayer::AddField(FieldSum& sum, RandomStream& random, const Vec2d& center,
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
    if (exposure_ && barren < exposure_.At(center.x, center.y, ground)) {
        return;
    }
    const double cover = Smoothstep(inside / kEdgeShare);
    const double wavelength = options_.wavelength * scale;
    const double wander = static_cast<double>(
        PerlinNoise2(static_cast<float>(dx / (kWanderWavelengths * wavelength)) + oy,
                     static_cast<float>(dy / (kWanderWavelengths * wavelength)) + ox, wanderSeed_));
    const double along = (dx * std::cos(azimuth) + dy * std::sin(azimuth)) / wavelength +
                         options_.sinuosity * wander + phase;
    const double t = along - std::floor(along);
    const double upwind = 1.0 - options_.leeFraction;
    const double profile = t < upwind
                               ? 0.5 * (1.0 - std::cos(kPi * t / upwind))
                               : 0.5 * (1.0 + std::cos(kPi * (t - upwind) / options_.leeFraction));
    sum.cover += cover;
    sum.relief += cover * options_.height * scale * profile;
}

BedformLayer::FieldSum BedformLayer::SumFields(double x, double y, const Ground* ground) const {
    FieldSum sum;
    if (density_ <= 0.0) {
        return sum;
    }
    exposure_.Require(ground);
    const double reach = kRadiusMax * (1.0 + kRagged) * cell_;
    for (const CellIndex& cell :
         cells_->CellsCovering(surface_.Expand({x, y, x, y}, reach), cell_)) {
        RandomStream random = LatticeStream(options_.seed, kBedformStream, cell.i, cell.j);
        const double chance = density_ * cells_->CellArea(cell, cell_) / (cell_ * cell_);
        if (random.NextDouble() >= chance) {
            continue;
        }
        const double u = random.NextDouble();
        const double v = random.NextDouble();
        const Vec2d center = cells_->SamplePoint(u, v, cell, cell_);
        AddField(sum, random, center, surface_.Offset(center, {x, y}), ground);
    }
    return sum;
}

double BedformLayer::CoverAt(double x, double y, const Ground* ground) const {
    return std::min(1.0, SumFields(x, y, ground).cover);
}

float BedformLayer::HeightAt(float height, double x, double y, double spacing,
                             const Ground* ground) const {
    const double weight = InSource(limit_.get(), options_.wavelength, x, y)
                              ? 0.0
                              : ResolutionWeight(options_.wavelength, spacing, options_.fade);
    if (weight <= 0.0) {
        return height;
    }
    const FieldSum sum = SumFields(x, y, ground);
    if (sum.cover <= 0.0) {
        return height;
    }
    return static_cast<float>(static_cast<double>(height) +
                              weight * sum.relief / std::max(1.0, sum.cover));
}

void BedformLayer::CheckContext(const LayerContext& context) const {
    exposure_.Require(context.ground);
}

}  // namespace terraingen
