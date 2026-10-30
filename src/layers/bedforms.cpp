// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/bedforms.h"

#include "layers/lattice.h"
#include "terraingen/geometry/geometry.h"
#include "terraingen/layers/bedforms_fields.h"
#include "terraingen/layers/source_resolved.h"
#include "terraingen/noise/perlin.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/grid.h"

#include <algorithm>
#include <cmath>
#include <memory>
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
    CheckRanges(options);
    if (options.exposure) {
        ValidateExposureParams(*options.exposure);
    }
    ValidateResolutionFade(options.fade);
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

BedformLayer::BedformLayer(BedformOptions options) : options_(options) {
    ValidateBedformOptions(options_);
    cell_ = options_.fieldSize / (kRadiusMin + kRadiusMax);
    const double meanArea = kPi * (std::pow(kRadiusMax, 3.0) - std::pow(kRadiusMin, 3.0)) /
                            (3.0 * (kRadiusMax - kRadiusMin));
    density_ = std::min(1.0, -std::log1p(-options_.cover) / meanArea);
    wanderSeed_ = SubstreamSeed(options_.seed, kBedformWanderStream);
    exposure_ = LayerExposure(options_.exposure, options_.exposureSpacing, kExposureReader);
}

LayerPtr BedformLayer::Reseeded(std::uint64_t seed) const {
    BedformOptions options = options_;
    options.seed = ReseedWord(seed, 0);
    return std::make_shared<BedformLayer>(options);
}

void BedformLayer::AddField(FieldSum& sum, RandomStream& random, const Vec2d& center,
                            const Vec2d& offset, const LayerContext& context) const {
    const double barren = random.NextDouble();
    const auto ox = static_cast<float>(97.0 * random.NextDouble());
    const auto oy = static_cast<float>(97.0 * random.NextDouble());
    const double radius = (kRadiusMin + (kRadiusMax - kRadiusMin) * random.NextDouble()) * cell_;
    const double scale = kWavelengthMin + kWavelengthSpan * random.NextDouble();
    const double azimuth =
        (options_.windAzimuthDegrees * kDegree) +
        (options_.azimuthSpreadDegrees * kDegree) * (2.0 * random.NextDouble() - 1.0);
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
        barren < exposure_.At(center.x, center.y, context.ground, context.SurfaceOrPlane())) {
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

BedformLayer::FieldSum BedformLayer::SumFields(double x, double y,
                                               const LayerContext& context) const {
    FieldSum sum;
    if (density_ <= 0.0) {
        return sum;
    }
    exposure_.Require(context.ground);
    const Surface& surface = context.SurfaceOrPlane();
    const std::shared_ptr<const SurfaceCells> cells = ThreadCells(surface);
    const double reach = kRadiusMax * (1.0 + kRagged) * cell_;
    for (const CellIndex& cell : cells->CellsCovering(surface.Expand({x, y, x, y}, reach), cell_)) {
        RandomStream random = LatticeStream(options_.seed, kBedformStream, cell.i, cell.j);
        const double chance = density_ * cells->CellArea(cell, cell_) / (cell_ * cell_);
        if (random.NextDouble() >= chance) {
            continue;
        }
        const double u = random.NextDouble();
        const double v = random.NextDouble();
        const Vec2d center = cells->SamplePoint(u, v, cell, cell_);
        AddField(sum, random, center, surface.Offset(center, {x, y}), context);
    }
    return sum;
}

double BedformLayer::MaterialCoverAt(double x, double y, const LayerContext& context) const {
    return std::min(1.0, SumFields(x, y, context).cover);
}

float BedformLayer::HeightAt(float height, double x, double y, const LayerContext& context) const {
    const double resolved = SourceResolvedSize(context.source, options_.sourceResolvesPixels,
                                               context.SurfaceOrPlane(), x, y);
    const double weight =
        options_.wavelength >= resolved
            ? 0.0
            : ResolutionWeight(options_.wavelength, context.spacing, options_.fade);
    if (weight <= 0.0) {
        return height;
    }
    const FieldSum sum = SumFields(x, y, context);
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
