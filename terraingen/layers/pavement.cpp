#include "terraingen/layers/pavement.h"

#include "terraingen/layers/lattice.h"
#include "terraingen/noise/perlin.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/surface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

constexpr const char* kExposureReader = "A pavement that reads the exposure field";
/// Plate sizes per lattice unit of the coarse and fine edge warp, and the fine warp's share.
constexpr double kWarpScale = 3.0;
constexpr double kFineWarpScale = 0.75;
constexpr double kFineWarp = 0.35;
/// Cells searched each way for the nearest plate sites; enough for any jitter in [0, 1].
constexpr std::int64_t kRings = 2;
/// The most crack octaves.
constexpr int kMaxOctaves = 8;

/// One plate: its site and the plane it lies in.
struct Site {
    Vec2d center;
    double step = 0.0;
    double slope = 0.0;
    Vec2d downhill;
    std::uint32_t id = 0;

    double HeightAt(const Vec2d& p) const {
        return step + slope * ((p.x - center.x) * downhill.x + (p.y - center.y) * downhill.y);
    }
};

/// The plate sites of the cells around a point, and the one nearest it.
struct Sites {
    std::array<Site, (2 * kRings + 1) * (2 * kRings + 1)> sites;
    std::size_t count = 0;
    std::size_t nearest = 0;
};

/// The sites of plates `size` across, their sites strayed by `jitter`, within `kRings` cells of
/// point `p`.
Sites PlateSites(const Vec2d& p, double size, double jitter, double maxStep, double maxTilt,
                 std::uint32_t seed) {
    Sites around;
    double best = std::numeric_limits<double>::infinity();
    ForEachLatticeCell(p, {size, size}, kRings, [&](std::int64_t gx, std::int64_t gy) {
        RandomStream random = LatticeStream(seed, kPavementStream, gx, gy);
        const double u = random.NextDouble();
        const double v = random.NextDouble();
        Site& site = around.sites[around.count];
        site.center = {(static_cast<double>(gx) + 0.5 + jitter * (u - 0.5)) * size,
                       (static_cast<double>(gy) + 0.5 + jitter * (v - 0.5)) * size};
        site.step = maxStep * (2.0 * random.NextDouble() - 1.0);
        site.slope = std::tan(maxTilt * random.NextDouble());
        const double turn = 2.0 * kPi * random.NextDouble();
        site.downhill = {std::cos(turn), std::sin(turn)};
        site.id = random.NextU32();
        const double d = std::hypot(p.x - site.center.x, p.y - site.center.y);
        if (d < best) {
            best = d;
            around.nearest = around.count;
        }
        ++around.count;
    });
    return around;
}

/// The distance from `p` to the nearest edge of the plate of the site nearest it, and the site
/// across that edge.
std::pair<double, std::size_t> NearestEdge(const Sites& around, const Vec2d& p) {
    const Site& own = around.sites[around.nearest];
    double edge = std::numeric_limits<double>::infinity();
    std::size_t neighbor = around.nearest;
    for (std::size_t k = 0; k < around.count; ++k) {
        if (k == around.nearest) {
            continue;
        }
        const Vec2d center = around.sites[k].center;
        const Vec2d across{center.x - own.center.x, center.y - own.center.y};
        const double length = std::hypot(across.x, across.y);
        const Vec2d middle{0.5 * (center.x + own.center.x), 0.5 * (center.y + own.center.y)};
        const double d =
            ((middle.x - p.x) * across.x + (middle.y - p.y) * across.y) / std::max(length, 1e-12);
        if (d < edge) {
            edge = d;
            neighbor = k;
        }
    }
    return {edge, neighbor};
}

}  // namespace

void ValidatePavementOptions(const PavementOptions& options) {
    RequirePositive(options.plateSize, "Pavement plateSize");
    RequirePositive(options.crackWidth, "Pavement crackWidth");
    RequirePositive(options.exposureSpacing, "Pavement exposureSpacing");
    RequireNonNegative(options.crackDepth, "Pavement crackDepth");
    RequireNonNegative(options.edgeRounding, "Pavement edgeRounding");
    RequireNonNegative(options.maxStep, "Pavement maxStep");
    RequireNonNegative(options.warp, "Pavement warp");
    if (!(options.maxTilt >= 0.0 && options.maxTilt < 0.5 * kPi)) {
        throw std::invalid_argument("Pavement maxTilt must be in [0, 90) deg");
    }
    RequireUnitInterval(options.jitter, "Pavement jitter");
    if (!(options.octaves >= 1 && options.octaves <= kMaxOctaves && options.lacunarity > 1.0 &&
          std::isfinite(options.lacunarity) && options.gain > 0.0 && options.gain <= 1.0)) {
        throw std::invalid_argument(
            "Pavement needs 1 to 8 octaves, a lacunarity above 1 and a gain in (0, 1]");
    }
    RequireFinite(options.anchor.x, "Pavement anchor x");
    RequireFinite(options.anchor.y, "Pavement anchor y");
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
    : FormulaLayer(surface), options_(options), plane_(surface, options.anchor) {
    ValidatePavementOptions(options_);
    if (options_.exposure) {
        exposure_.emplace(*options_.exposure, surface_);
    }
    double size = 1.0;
    double relief = 1.0;
    for (int k = 0; k < options_.octaves; ++k) {
        levels_.push_back(
            {options_.plateSize / size, relief * options_.crackWidth, relief * options_.crackDepth,
             relief * options_.edgeRounding, relief * options_.maxTilt, relief * options_.maxStep,
             SubstreamSeed(options_.seed, kPavementOctaveStream, static_cast<std::uint32_t>(k))});
        size *= options_.lacunarity;
        relief *= options_.gain;
    }
}

Vec2d PavementLayer::Warp(const Vec2d& q, double size, std::uint32_t seed) const {
    const auto warp = [&](double scale, double ox, double oy, std::uint32_t field) {
        return static_cast<double>(
            PerlinNoise2Precise(q.x / (scale * size) + ox, q.y / (scale * size) + oy,
                                SubstreamSeed(seed, kPavementWarpStream, field)));
    };
    const double reach = options_.warp * size;
    return {q.x + reach * (warp(kWarpScale, 0.0, 0.0, 0u) +
                           kFineWarp * warp(kFineWarpScale, 5.3, -8.1, 2u)),
            q.y + reach * (warp(kWarpScale, 31.7, -12.3, 1u) +
                           kFineWarp * warp(kFineWarpScale, -17.9, 4.4, 3u))};
}

PavementLayer::Plate PavementLayer::PlateAt(const Vec2d& q, const Level& level, std::uint32_t seed,
                                            double crackWeight) const {
    const Vec2d p = Warp(q, level.size, seed);
    const Sites around =
        PlateSites(p, level.size, options_.jitter, level.maxStep, level.maxTilt, seed);
    const Site& own = around.sites[around.nearest];
    const auto [edge, neighbor] = NearestEdge(around, p);
    const double shoulder = 0.5 * level.crackWidth + level.rounding;
    const double crack = 1.0 - Smoothstep(edge / shoulder);
    const double ownHeight = own.HeightAt(p);
    const double plate = ownHeight + 0.5 * crack * (around.sites[neighbor].HeightAt(p) - ownHeight);
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
        const Plate plate = PlateAt(
            q, level, SubstreamSeed(level.seed, kPavementParentStream, parent), crackWeight);
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
    const std::optional<Vec2d> q = plane_.Project(x, y);
    return q ? Relief(*q, 0.0) : 0.0;
}

double PavementLayer::EdgeDistanceAt(double x, double y) const {
    const std::optional<Vec2d> q = plane_.Project(x, y);
    return q ? PlateAt(*q, levels_.front(),
                       SubstreamSeed(levels_.front().seed, kPavementParentStream, 0u), 1.0)
                   .edge
             : std::numeric_limits<double>::infinity();
}

double PavementLayer::MaterialCoverAt(double x, double y, double /*spacing*/,
                                      const Ground* ground) const {
    return plane_.Project(x, y) ? ExposureWeight(x, y, ground) : 0.0;
}

float PavementLayer::HeightAt(float height, double x, double y, double spacing,
                              const Ground* ground) const {
    const std::optional<Vec2d> q = plane_.Project(x, y);
    if (!q || ResolutionWeight(options_.plateSize, spacing, options_.fade) <= 0.0) {
        return height;
    }
    const double exposure = ExposureWeight(x, y, ground);
    if (exposure <= 0.0) {
        return height;
    }
    return static_cast<float>(static_cast<double>(height) + exposure * Relief(*q, spacing));
}

void PavementLayer::CheckContext(const LayerContext& context) const {
    if (exposure_) {
        RequireGround(context.ground, kExposureReader);
    }
}

}  // namespace terraingen
