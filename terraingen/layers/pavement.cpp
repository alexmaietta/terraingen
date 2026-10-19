#include "terraingen/layers/pavement.h"

#include "terraingen/noise/perlin.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/surface.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace terraingen {

namespace {

constexpr std::uint32_t kPavementStream = 0x50415645u;
constexpr const char* kExposureReader = "A pavement that reads the exposure field";
/// Plate sizes per lattice unit of the coarse and fine edge warp, and the fine warp's share.
constexpr double kWarpScale = 3.0;
constexpr double kFineWarpScale = 0.75;
constexpr double kFineWarp = 0.35;
/// Cells searched each way for the nearest plate sites; enough for any jitter in [0, 1].
constexpr std::int64_t kRings = 2;
/// The most crack octaves.
constexpr int kMaxOctaves = 8;
/// Seed step between octaves.
constexpr std::uint32_t kOctaveSeed = 101u;
/// Mixes a parent plate's id into its finer octave's seed, so finer cracks end at its edges.
constexpr std::uint32_t kParentMix = 0x9e3779b9u;

/// One plate: its site and the plane it lies in.
struct Site {
    Vec2d centre;
    double step = 0.0;
    double slope = 0.0;
    Vec2d downhill;
    std::uint32_t id = 0;

    double HeightAt(const Vec2d& p) const {
        return step + slope * ((p.x - centre.x) * downhill.x + (p.y - centre.y) * downhill.y);
    }
};

}  // namespace

void ValidatePavementOptions(const PavementOptions& options) {
    RequirePositive(options.plateSize, "Pavement plateSize");
    RequirePositive(options.crackWidth, "Pavement crackWidth");
    RequirePositive(options.exposureSpacing, "Pavement exposureSpacing");
    if (!(IsNonNegative(options.crackDepth) && IsNonNegative(options.edgeRounding) &&
          IsNonNegative(options.maxStep) && IsNonNegative(options.warp))) {
        throw std::invalid_argument(
            "Pavement crack depth, edge rounding, step and warp must be finite and not negative");
    }
    if (!(options.maxTilt >= 0.0 && options.maxTilt < 0.5 * std::numbers::pi)) {
        throw std::invalid_argument("Pavement maxTilt must be in [0, 90) deg");
    }
    if (!(options.jitter >= 0.0 && options.jitter <= 1.0)) {
        throw std::invalid_argument("Pavement jitter must be in [0, 1]");
    }
    if (!(options.octaves >= 1 && options.octaves <= kMaxOctaves && options.lacunarity > 1.0 &&
          std::isfinite(options.lacunarity) && options.gain > 0.0 && options.gain <= 1.0)) {
        throw std::invalid_argument(
            "Pavement needs 1 to 8 octaves, a lacunarity above 1 and a gain in (0, 1]");
    }
    if (!(std::isfinite(options.anchor.x) && std::isfinite(options.anchor.y))) {
        throw std::invalid_argument("Pavement anchor must be finite");
    }
    if (options.exposure) {
        ValidateExposureParams(*options.exposure);
    }
    ValidateResolutionFade(options.fade);
}

PavementOptions MarsDesiccationRidges() {
    PavementOptions ridges;
    ridges.plateSize = 0.02;
    ridges.jitter = 0.6;
    ridges.warp = 0.15;
    ridges.raised = true;
    ridges.crackDepth = 0.005;
    ridges.crackWidth = 0.002;
    ridges.edgeRounding = 0.001;
    ridges.maxTilt = 0.0;
    ridges.maxStep = 0.0;
    return ridges;
}

PavementLayer::PavementLayer(PavementOptions options, Surface surface)
    : FormulaLayer(surface), options_(options) {
    ValidatePavementOptions(options_);
    if (options_.exposure) {
        exposure_.emplace(*options_.exposure, surface_);
    }
    if (surface_.IsBody()) {
        anchorDirection_ = UnitVector(options_.anchor.x, options_.anchor.y);
    }
    double size = 1.0;
    double relief = 1.0;
    for (int k = 0; k < options_.octaves; ++k) {
        levels_.push_back({options_.plateSize / size, relief * options_.crackWidth,
                           relief * options_.crackDepth, relief * options_.edgeRounding,
                           relief * options_.maxTilt, relief * options_.maxStep,
                           options_.seed + kOctaveSeed * static_cast<std::uint32_t>(k)});
        size *= options_.lacunarity;
        relief *= options_.gain;
    }
}

bool PavementLayer::Planar(double x, double y, Vec2d& planar) const {
    if (!surface_.IsBody()) {
        planar = {x, y};
        return true;
    }
    if (Dot(UnitVector(x, y), anchorDirection_) <= 0.0) {
        return false;
    }
    planar = surface_.Offset(options_.anchor, {x, y});
    return true;
}

PavementLayer::Plate PavementLayer::PlateAt(const Vec2d& q, const Level& level, std::uint32_t seed,
                                            double crackWeight) const {
    const double size = level.size;
    const auto warp = [&](double scale, double ox, double oy, std::uint32_t salt) {
        return static_cast<double>(
            PerlinNoise2Precise(q.x / (scale * size) + ox, q.y / (scale * size) + oy, seed + salt));
    };
    const double reach = options_.warp * size;
    const Vec2d p{q.x + reach * (warp(kWarpScale, 0.0, 0.0, 7u) +
                                 kFineWarp * warp(kFineWarpScale, 5.3, -8.1, 9u)),
                  q.y + reach * (warp(kWarpScale, 31.7, -12.3, 8u) +
                                 kFineWarp * warp(kFineWarpScale, -17.9, 4.4, 10u))};
    const auto ix = static_cast<std::int64_t>(std::floor(p.x / size));
    const auto iy = static_cast<std::int64_t>(std::floor(p.y / size));
    Site sites[(2 * kRings + 1) * (2 * kRings + 1)];
    std::size_t count = 0;
    std::size_t nearest = 0;
    double best = std::numeric_limits<double>::infinity();
    for (std::int64_t gy = iy - kRings; gy <= iy + kRings; ++gy) {
        for (std::int64_t gx = ix - kRings; gx <= ix + kRings; ++gx) {
            RandomStream random(seed, kPavementStream, static_cast<std::uint32_t>(gx),
                                static_cast<std::uint32_t>(gy),
                                static_cast<std::uint32_t>((gx >> 32) ^ ((gy >> 32) << 16)));
            const double u = random.NextDouble();
            const double v = random.NextDouble();
            Site& site = sites[count];
            site.centre = {(static_cast<double>(gx) + 0.5 + options_.jitter * (u - 0.5)) * size,
                           (static_cast<double>(gy) + 0.5 + options_.jitter * (v - 0.5)) * size};
            site.step = level.maxStep * (2.0 * random.NextDouble() - 1.0);
            site.slope = std::tan(level.maxTilt * random.NextDouble());
            const double turn = 2.0 * std::numbers::pi * random.NextDouble();
            site.downhill = {std::cos(turn), std::sin(turn)};
            site.id = random.NextU32();
            const double d = std::hypot(p.x - site.centre.x, p.y - site.centre.y);
            if (d < best) {
                best = d;
                nearest = count;
            }
            ++count;
        }
    }
    const Site& own = sites[nearest];
    double edge = std::numeric_limits<double>::infinity();
    std::size_t neighbour = nearest;
    for (std::size_t k = 0; k < count; ++k) {
        if (k == nearest) {
            continue;
        }
        const Vec2d across{sites[k].centre.x - own.centre.x, sites[k].centre.y - own.centre.y};
        const double length = std::hypot(across.x, across.y);
        const Vec2d middle{0.5 * (sites[k].centre.x + own.centre.x),
                           0.5 * (sites[k].centre.y + own.centre.y)};
        const double d =
            ((middle.x - p.x) * across.x + (middle.y - p.y) * across.y) / std::max(length, 1e-12);
        if (d < edge) {
            edge = d;
            neighbour = k;
        }
    }
    const double shoulder = 0.5 * level.crackWidth + level.rounding;
    const double crack = 1.0 - Smoothstep(edge / shoulder);
    const double ownHeight = own.HeightAt(p);
    const double plate = ownHeight + 0.5 * crack * (sites[neighbour].HeightAt(p) - ownHeight);
    const double sign = options_.raised ? 1.0 : -1.0;
    return {plate + crackWeight * sign * level.crackDepth * crack, edge, 1.0 - crack, own.id};
}

double PavementLayer::Relief(const Vec2d& q, double spacing) const {
    double relief = 0.0;
    double interior = 1.0;
    std::uint32_t parent = 0;
    for (const Level& level : levels_) {
        const double plateWeight =
            spacing > 0.0 ? ResolutionWeight(level.size, spacing, options_.fade) : 1.0;
        if (plateWeight <= 0.0 || interior <= 0.0) {
            break;
        }
        const double crackWeight =
            spacing > 0.0
                ? ResolutionWeight(level.crackWidth + 2.0 * level.rounding, spacing, options_.fade)
                : 1.0;
        const Plate plate = PlateAt(q, level, level.seed ^ (parent * kParentMix), crackWeight);
        relief += interior * plateWeight * plate.relief;
        interior *= plate.interior;
        parent = plate.id;
    }
    return relief;
}

double PavementLayer::ExposureWeight(double x, double y, const Ground* ground) const {
    if (!exposure_) {
        return 1.0;
    }
    RequireGround(ground, kExposureReader);
    return exposure_->Exposure(*ground, x, y, options_.exposureSpacing);
}

double PavementLayer::ReliefAt(double x, double y) const {
    Vec2d q;
    return Planar(x, y, q) ? Relief(q, 0.0) : 0.0;
}

double PavementLayer::EdgeDistanceAt(double x, double y) const {
    Vec2d q;
    return Planar(x, y, q) ? PlateAt(q, levels_.front(), levels_.front().seed, 1.0).edge
                           : std::numeric_limits<double>::infinity();
}

double PavementLayer::MaterialCoverAt(double x, double y, double /*spacing*/,
                                      const Ground* ground) const {
    Vec2d q;
    return Planar(x, y, q) ? ExposureWeight(x, y, ground) : 0.0;
}

float PavementLayer::HeightAt(float height, double x, double y, double spacing,
                              const Ground* ground) const {
    Vec2d q;
    if (!Planar(x, y, q) || ResolutionWeight(options_.plateSize, spacing, options_.fade) <= 0.0) {
        return height;
    }
    const double exposure = ExposureWeight(x, y, ground);
    if (exposure <= 0.0) {
        return height;
    }
    return static_cast<float>(static_cast<double>(height) + exposure * Relief(q, spacing));
}

void PavementLayer::CheckContext(const LayerContext& context) const {
    if (exposure_) {
        RequireGround(context.ground, kExposureReader);
    }
}

}  // namespace terraingen
