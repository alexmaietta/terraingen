// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/albedo/crater_albedo.h"

#include "terraingen/albedo/bands.h"
#include "terraingen/albedo/zone_shadows.h"
#include "terraingen/layers/craters/secondaries.h"
#include "terraingen/layers/craters/shaping/irregularity.h"
#include "terraingen/noise/hash.h"
#include "terraingen/noise/value.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/log.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

constexpr std::size_t kBlockRows = 32;

/// How far `crater`'s ejecta edge reaches at `angle` radians around it, over its mean reach.
double LobeScale(double angle, std::uint32_t key, const CraterAlbedoTexture& texture) {
    if (texture.lobes == 0.0) {
        return 1.0;
    }
    const double ring = static_cast<double>(texture.lobeCount) / (2.0 * kPi);
    const double n = RingNoise(ring, angle, 0.0, key ^ kEjectaLobeStream, 2);
    return 1.0 + texture.lobes * std::clamp(n, -1.0, 1.0);
}

/// The share of the excess the filaments leave at `radii` radii and `angle` radians around a
/// crater: 1 along a filament, down to 1 - `streaks` between them.
double StreakShare(double radii, double angle, std::uint32_t key,
                   const CraterAlbedoTexture& texture) {
    if (texture.streaks == 0.0) {
        return 1.0;
    }
    const double ring = kRadian / texture.streakWidthDegrees;
    const double n = RingNoise(ring, angle, radii / texture.streakLengthRadii, key, 3);
    const double filament = Smoothstep(0.5 + 1.5 * n);
    return 1.0 - texture.streaks * (1.0 - filament);
}

/// One reflectance zone of a crater, drawn.
struct DrawnZone {
    double change = 0.0;
    double strength = 0.0;
    double resolution = 0.0;
    double reach = 0.0;
    double raggedness = 0.0;
    double ring = 0.0;
    double feather = 1.0;
    bool streaked = false;
    bool distal = false;
    std::uint32_t key = 0;

    /// How much of the change the zone gives at `distance` meters and `angle` radians from the
    /// crater's center, from 1 inside to 0 beyond its ragged edge, whose fingers bend and break
    /// with distance.
    double Cover(double distance, double angle) const {
        if (distance >= reach) {
            return 0.0;
        }
        const double finger = EdgeLobe(raggedness, ring, angle, 3.0 * distance / reach, key, 4);
        return SoftEdge(distance / (reach * finger), feather);
    }
};

/// `zone` drawn from the next draws of `random` around a crater `diameter` meters across with
/// `life` of its zone life left; nothing when the crater lacks it or it has faded.
std::optional<DrawnZone> DrawZone(const ReflectanceZone& zone, RandomStream& random,
                                  double diameter, double life, double fadeExponent,
                                  double resolution) {
    const double present = random.NextDouble();
    const double c1 = random.NextDouble();
    const double c2 = random.NextDouble();
    const double s1 = random.NextDouble();
    const double s2 = random.NextDouble();
    DrawnZone drawn;
    drawn.key = random.NextU32();
    if (present >= zone.share || life <= 0.0) {
        return std::nullopt;
    }
    drawn.change = zone.change.Sample(c1, c2);
    drawn.strength = std::pow(life, fadeExponent);
    drawn.resolution = resolution;
    drawn.reach = zone.coefficient * std::pow(diameter, zone.exponent) * zone.scale.Sample(s1, s2);
    drawn.raggedness = zone.raggedness;
    drawn.ring = kRadian / zone.fingerWidthDegrees;
    drawn.feather = zone.feather;
    return drawn;
}

}  // namespace

/// One ray, in the frame of its crater.
struct CraterAlbedo::Ray {
    double cos = 1.0;
    double sin = 0.0;
    double length = 0.0;
    double sinHalfWidth = 0.0;
    std::uint32_t seed = 0;

    /// The ray's excess at `along` meters from the crater's center along it and `lateral` across,
    /// for a crater of `radius` meters.
    double ExcessAt(double along, double lateral, double radius, const CraterRays& rays) const {
        if (along <= radius || along >= length) {
            return 0.0;
        }
        const double half = std::fmin(along, 2.0 * radius) * sinHalfWidth;
        if (lateral >= half) {
            return 0.0;
        }
        const double q = 1.0 - (lateral / half) * (lateral / half);
        double strength = rays.excess * q * q * (length - along) / (length - radius);
        if (rays.patchiness > 0.0) {
            const double wavelength = rays.patchRadii * radius;
            const double u = along / wavelength;
            const double iu = std::floor(u);
            const float noise = ValueNoise2(static_cast<std::int32_t>(iu), 0,
                                            static_cast<float>(u - iu), 0.5f, seed);
            strength *= 1.0 - rays.patchiness * (0.5 + 0.5 * static_cast<double>(noise));
        }
        return strength;
    }
};

/// A crater's bright material, ready to evaluate at points.
struct CraterAlbedo::Prepared {
    Crater crater;
    CraterOutline outline;
    KernelOutline profile;  ///< `outline` as the crater kernel reads it.
    double weight = 0.0;
    double reach = 0.0;
    std::vector<Ray> rays;
    std::vector<DrawnZone> zones;
    std::shared_ptr<const detail::ShadowProfile> shadow;

    /// The relative reflectance change of the zones at (`x`, `y`), inner zones covering outer as
    /// long as they last, the bright proximal zone broken by `texture`'s filaments.
    double Change(const CraterAlbedoTexture& texture, const Surface& surface, double x,
                  double y) const {
        if (zones.empty()) {
            return 0.0;
        }
        const Vec2d offset = surface.Offset({crater.x, crater.y}, {x, y});
        const double distance = std::hypot(offset.x, offset.y);
        const double angle = std::atan2(offset.y, offset.x);
        const double shade = shadow ? shadow->Shade(distance, angle) : 0.0;
        ChangeOverlay overlay;
        for (const DrawnZone& zone : zones) {
            const double cover = zone.Cover(distance, angle) * (zone.distal ? 1.0 - shade : 1.0);
            if (cover == 0.0) {
                continue;
            }
            const double streak = zone.streaked
                                      ? StreakShare(2.0 * distance / crater.diameter, angle,
                                                    CraterKey(crater.id, texture.seed), texture)
                                      : 1.0;
            overlay.Add(cover * zone.strength, zone.resolution * streak * zone.change);
        }
        return overlay.Change();
    }

    /// The excess optical maturity at (`x`, `y`).
    double Excess(const CraterAlbedoOptions& o, const Surface& surface, double x, double y) const {
        if (weight == 0.0) {
            return 0.0;
        }
        const double radii = profile.ProfileDistance(surface.Offset({crater.x, crater.y}, {x, y})) /
                             (0.5 * crater.diameter);
        const CraterAlbedoTexture& texture = o.texture;
        const bool textured = texture.streaks > 0.0 || texture.lobes > 0.0;
        const Vec2d offset = surface.Offset({crater.x, crater.y}, {x, y});
        const double angle = textured ? std::atan2(offset.y, offset.x) : 0.0;
        const std::uint32_t key = textured ? CraterKey(crater.id, texture.seed) : 0u;
        if (radii < 1.0) {
            return InteriorExcess(o, radii, angle, key);
        }
        const double excess = OutsideExcess(o, offset, radii, angle, key);
        if (excess == 0.0) {
            return 0.0;
        }
        const double streak = StreakShare(radii, angle, key, texture);
        if (texture.continuousRadii > 0.0) {
            const double ramp = SmoothRamp(radii, 1.0, texture.continuousRadii);
            return weight * excess * (1.0 - (1.0 - streak) * ramp);
        }
        return weight * excess * streak;
    }

    /// The weighted excess inside the rim, `radii` radii and `angle` radians from the center,
    /// its filaments keyed by `key`.
    double InteriorExcess(const CraterAlbedoOptions& o, double radii, double angle,
                          std::uint32_t key) const {
        const CraterAlbedoTexture& texture = o.texture;
        double interior = o.interiorExcess;
        const double wall = SmoothRamp(radii, 0.3, 0.6);
        if (texture.floorShare < 1.0) {
            interior *= texture.floorShare + (1.0 - texture.floorShare) * wall;
        }
        const double streak = StreakShare(radii, angle, key, texture);
        return weight * interior * (1.0 - (1.0 - streak) * wall);
    }

    /// The unweighted excess of the ejecta and rays beyond the rim at `offset` meters from the
    /// center, `radii` radii and `angle` radians, before the filaments.
    double OutsideExcess(const CraterAlbedoOptions& o, const Vec2d& offset, double radii,
                         double angle, std::uint32_t key) const {
        double excess = 0.0;
        const double lobed = radii / LobeScale(angle, key, o.texture);
        if (lobed < o.ejectaReachRadii) {
            const double edge = std::pow(o.ejectaReachRadii, -o.ejectaExponent);
            excess = o.ejectaExcess * (std::pow(std::fmax(lobed, 1.0), -o.ejectaExponent) - edge) /
                     (1.0 - edge);
        }
        const double radius = 0.5 * crater.diameter;
        for (const Ray& ray : rays) {
            const double along = offset.x * ray.cos + offset.y * ray.sin;
            const double lateral = std::fabs(offset.y * ray.cos - offset.x * ray.sin);
            excess += ray.ExcessAt(along, lateral, radius, o.rays);
        }
        return excess;
    }
};

CraterAlbedo::CraterAlbedo(std::shared_ptr<const CraterLayer> craters, CraterAlbedoOptions options)
    : craters_(std::move(craters)), options_(options) {
    if (!craters_) {
        throw std::invalid_argument("Crater albedo needs a crater layer");
    }
    ValidateCraterAlbedoOptions(options_);
}

bool CraterAlbedo::Eligible(const Crater& crater, RandomStream& random) const {
    return options_.selection.Includes(crater) && random.NextDouble() < options_.share;
}

CraterAlbedo::Ray CraterAlbedo::DrawRay(RandomStream& random, double median,
                                        const CraterImpact& impact,
                                        const RayDirection* direction) const {
    const CraterRays& rays = options_.rays;
    const ObliqueImpacts& oblique = rays.oblique;
    const double asymmetry = (impact.speedRatio - 1.0) / (impact.speedRatio + 1.0);
    const double zone = oblique.zoneHalfWidthDegrees * kDegree;
    const double drawn = 2.0 * kPi * random.NextDouble();
    const double azimuth = direction != nullptr ? direction->azimuth : drawn;
    const double l1 = random.NextDouble();
    const double l2 = random.NextDouble();
    const double w1 = random.NextDouble();
    const double w2 = random.NextDouble();
    Ray ray;
    ray.cos = std::cos(azimuth);
    ray.sin = std::sin(azimuth);
    ray.length = median * std::exp(rays.lengthSpread.Sample(l1, l2));
    const double fromDownrange = std::remainder(azimuth - impact.heading, 2.0 * kPi);
    if (asymmetry > 0.0) {
        const double speed = 1.0 + asymmetry * std::cos(fromDownrange);
        ray.length *= speed * speed / (1.0 + 0.5 * asymmetry * asymmetry);
    }
    const bool uprange = std::fabs(fromDownrange) > kPi - zone;
    const bool downrange = std::fabs(fromDownrange) < zone;
    if ((oblique.enabled && impact.angleDegrees < oblique.forbiddenDegrees && uprange) ||
        (oblique.enabled && impact.angleDegrees < oblique.butterflyDegrees && downrange)) {
        ray.length = 0.0;
    }
    ray.sinHalfWidth = direction != nullptr
                           ? direction->sinHalfWidth
                           : std::sin(0.5 * rays.widthDegrees.Sample(w1, w2) * kDegree);
    ray.seed = random.NextU32();
    return ray;
}

void CraterAlbedo::AddRays(Prepared& prepared, RandomStream& random) const {
    const Crater& crater = prepared.crater;
    const double c1 = random.NextDouble();
    const double c2 = random.NextDouble();
    const std::vector<RayDirection> directions = options_.rays.layout
                                                     ? RayDirections(crater, *options_.rays.layout)
                                                     : std::vector<RayDirection>{};
    const auto count = options_.rays.layout
                           ? static_cast<int>(directions.size())
                           : static_cast<int>(std::lround(options_.rays.count.Sample(c1, c2)));
    const double median = options_.rays.MedianLength(crater.diameter);
    const CraterImpact impact = Impact(crater);
    for (int i = 0; i < count; ++i) {
        const Ray ray =
            DrawRay(random, median, impact,
                    options_.rays.layout ? &directions[static_cast<std::size_t>(i)] : nullptr);
        if (ray.length > 0.5 * crater.diameter) {
            prepared.reach = std::fmax(prepared.reach, ray.length);
            prepared.rays.push_back(ray);
        }
    }
}

void CraterAlbedo::AddZones(Prepared& prepared, double left, double resolution) const {
    const CraterReflectanceZones& z = options_.zones;
    const Crater& crater = prepared.crater;
    const IdWords id = SplitId(crater.id);
    RandomStream random(z.seed, kAlbedoZoneStream, id.low, id.high);
    const bool immature = z.excavationRatio * crater.diameter > z.matureDepth;
    std::vector<DrawnZone> distal;
    for (const ReflectanceZone* zone : z.All()) {
        const double life = zone == &z.proximalHigh && !immature ? 0.0
                            : zone->retentionMyr
                                ? std::fmin(left, LifeLeft(crater.ageGyr, *zone->retentionMyr))
                                : left;
        std::optional<DrawnZone> drawn =
            DrawZone(*zone, random, crater.diameter, life, options_.fadeExponent, resolution);
        if (!drawn) {
            continue;
        }
        drawn->streaked = zone == &z.proximalHigh;
        drawn->distal = zone == &z.distalHigh || zone == &z.distalLow;
        prepared.reach = std::fmax(prepared.reach, drawn->reach);
        (drawn->distal ? distal : prepared.zones).push_back(*drawn);
    }
    std::sort(distal.begin(), distal.end(),
              [](const DrawnZone& a, const DrawnZone& b) { return a.reach < b.reach; });
    prepared.zones.insert(prepared.zones.end(), distal.begin(), distal.end());
}

CraterAlbedo::Prepared CraterAlbedo::Prepare(const Crater& crater, double spacing) const {
    Prepared prepared;
    prepared.crater = crater;
    const CraterAlbedoOptions& o = options_;
    const IdWords id = SplitId(crater.id);
    RandomStream random(o.seed, kCraterAlbedoStream, id.low, id.high);
    if (!Eligible(crater, random)) {
        return prepared;
    }
    const double left = LifeLeft(crater.ageGyr, o.retention.Myr(crater.diameter));
    const double resolution = ResolutionWeight(crater.diameter, spacing, o.fade);
    if (left <= 0.0 || resolution == 0.0) {
        return prepared;
    }
    prepared.weight = std::pow(left, o.fadeExponent) * resolution;
    prepared.outline = IrregularOutline(crater, craters_->Options().apply.irregularity);
    prepared.profile = KernelOutline(prepared.outline, crater.diameter);
    prepared.reach = o.ejectaReachRadii * (0.5 * crater.diameter) * prepared.outline.MaxScale() *
                     (1.0 + o.texture.lobes);
    if (o.rays.enabled) {
        AddRays(prepared, random);
    }
    if (o.zones.enabled && crater.diameter <= o.zones.maxDiameter) {
        AddZones(prepared, left, resolution);
    }
    return prepared;
}

double CraterAlbedo::Excess(const Crater& crater, double x, double y, double spacing) const {
    return Prepare(crater, spacing).Excess(options_, craters_->Options().surface, x, y);
}

double CraterAlbedo::ReflectanceChange(const Crater& crater, double x, double y, double spacing,
                                       const Ground* ground) const {
    Prepared prepared = Prepare(crater, spacing);
    if (ground != nullptr && options_.zones.shadows.enabled) {
        AddShadows(prepared, *ground, PointBounds(x, y));
    }
    return prepared.Change(options_.texture, craters_->Options().surface, x, y);
}

void CraterAlbedo::AddShadows(Prepared& prepared, const Ground& ground,
                              const Bounds2d& region) const {
    const ZoneShadows& s = options_.zones.shadows;
    double reach = 0.0;
    for (const DrawnZone& zone : prepared.zones) {
        reach = zone.distal ? std::fmax(reach, zone.reach) : reach;
    }
    const Crater& crater = prepared.crater;
    const double radius = 0.5 * crater.diameter;
    if (reach <= radius) {
        return;
    }
    const Vec2d center{crater.x, crater.y};
    const Surface& surface = craters_->Options().surface;
    const detail::RayWindow window =
        detail::RaysOver(center, region, surface, static_cast<std::size_t>(s.azimuths));
    auto profile = std::make_shared<detail::ShadowProfile>(
        detail::MakeShadowProfile(s, radius, reach, window));
    const std::vector<Vec2d> points = profile->Points(center, surface);
    std::vector<float> heights(points.size());
    ground.HeightsAt(points, heights, profile->step);
    profile->TraceJets(heights, std::tan(s.jetAngleDegrees * kDegree) * profile->step);
    prepared.shadow = std::move(profile);
}

CraterImpact CraterAlbedo::Impact(const Crater& crater) const {
    const ObliqueImpacts& oblique = options_.rays.oblique;
    CraterImpact impact;
    if (!oblique.enabled) {
        return impact;
    }
    const IdWords id = SplitId(crater.id);
    RandomStream random(options_.seed, kObliqueRayStream, id.low, id.high);
    impact.angleDegrees = std::asin(std::sqrt(random.NextDouble())) * kRadian;
    const double h = random.NextDouble();
    impact.heading = oblique.headingDegrees ? *oblique.headingDegrees * kDegree : 2.0 * kPi * h;
    const double r1 = random.NextDouble();
    const double r2 = random.NextDouble();
    if (impact.angleDegrees < oblique.asymmetricDegrees) {
        impact.speedRatio = oblique.downrangeSpeedRatio.Sample(r1, r2);
    }
    return impact;
}

double CraterAlbedo::Reach(const Crater& crater) const {
    return Prepare(crater, 0.0).reach;
}

double CraterAlbedo::GatherMargin() const {
    const double largest = craters_->MaxFeatureDiameter();
    const double rayReach = options_.rays.enabled
                                ? options_.rays.MedianLength(largest) *
                                      std::exp(options_.rays.lengthSpread.Highest()) *
                                      options_.rays.oblique.MaxStretch()
                                : 0.0;
    return std::fmax(rayReach, 0.5 * largest * options_.ejectaReachRadii *
                                   MaxOutlineScale(craters_->Options().apply.irregularity) *
                                   (1.0 + options_.texture.lobes));
}

void CraterAlbedo::TakePrepared(const CraterList& list, const Bounds2d& region, double spacing,
                                std::vector<Prepared>& prepared) const {
    const Surface& surface = craters_->Options().surface;
    for (std::size_t i = 0; i < list.Size(); ++i) {
        Prepared one = Prepare(list.At(i), spacing);
        if (one.weight == 0.0) {
            continue;
        }
        const Crater& c = one.crater;
        if (surface.Contains(surface.Expand(region, one.reach), {c.x, c.y})) {
            prepared.push_back(std::move(one));
        }
    }
}

namespace {

/// Sorts `prepared` by crater ID and drops repeats.
template <typename Prepared> void SortUniqueById(std::vector<Prepared>& prepared) {
    std::sort(prepared.begin(), prepared.end(),
              [](const Prepared& a, const Prepared& b) { return a.crater.id < b.crater.id; });
    prepared.erase(std::unique(prepared.begin(), prepared.end(),
                               [](const Prepared& a, const Prepared& b) {
                                   return a.crater.id == b.crater.id;
                               }),
                   prepared.end());
}

/// The craters of `prepared` whose reach meets the rows of `raster` from `row0` to `row1`.
template <typename Prepared>
std::vector<const Prepared*> NearRows(const std::vector<Prepared>& prepared,
                                      const Raster<float>& raster, std::size_t row0,
                                      std::size_t row1, const Surface& surface) {
    const double ya = raster.CellCenter(0, row0).y;
    const double yb = raster.CellCenter(0, row1 - 1).y;
    std::vector<const Prepared*> near;
    for (const Prepared& p : prepared) {
        const double reachY = p.reach / surface.UnitLength();
        if (p.crater.y + reachY >= std::fmin(ya, yb) && p.crater.y - reachY <= std::fmax(ya, yb)) {
            near.push_back(&p);
        }
    }
    return near;
}

/// Moves `value` to the reflectance at the background maturity plus `excess`, capped, then
/// changes it by the relative reflectance `change`, never below 0.
void Brighten(AlbedoSample& value, std::size_t bands, double excess, double change,
              const CraterAlbedoOptions& o) {
    if (excess == 0.0 && change == 0.0) {
        return;
    }
    const double factor = std::fmax(0.0, 1.0 + change);
    if (excess == 0.0) {
        ScaleBands(value, bands, factor);
        return;
    }
    const double maturity = std::fmin(o.backgroundMaturity + excess, o.maxMaturity);
    MatureBands(value, bands, o.backgroundMaturity, maturity, o.originReflectance, factor);
}

}  // namespace

std::vector<CraterAlbedo::Prepared> CraterAlbedo::Gather(const Bounds2d& region, double spacing,
                                                         const Ground* ground) const {
    const double margin = GatherMargin();
    const Surface& surface = craters_->Options().surface;
    std::vector<Prepared> prepared;
    TakePrepared(craters_->Craters(surface.Expand(region, margin), spacing), region, spacing,
                 prepared);
    const CraterReflectanceZones& zones = options_.zones;
    if (zones.enabled) {
        for (double high = std::fmin(craters_->MaxFeatureDiameter(), zones.maxDiameter);
             zones.MaxReach(high) > margin; high *= 0.5) {
            TakePrepared(craters_->Craters(surface.Expand(region, zones.MaxReach(high)), spacing,
                                           0.5 * high, high),
                         region, spacing, prepared);
        }
    }
    SortUniqueById(prepared);
    if (ground != nullptr && zones.enabled && zones.shadows.enabled) {
        std::size_t shadowed = 0;
        for (Prepared& p : prepared) {
            AddShadows(p, *ground, region);
            shadowed += p.shadow ? 1u : 0u;
        }
        LogDebug("albedo", "Sampled the ground for the zone shadows of ", shadowed, " craters");
    }
    return prepared;
}

void CraterAlbedo::ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                           const AlbedoContext& context) const {
    const Surface& surface = craters_->Options().surface;
    double excess = 0.0;
    double change = 0.0;
    for (const Prepared& p : Gather(PointBounds(x, y), context.spacing, context.ground)) {
        excess += p.Excess(options_, surface, x, y);
        change += p.Change(options_.texture, surface, x, y);
    }
    Brighten(value, bands, excess, change, options_);
}

void CraterAlbedo::Apply(std::vector<Raster<float>>& bands, const AlbedoContext& context) const {
    if (bands.empty()) {
        return;
    }
    const Surface& surface = craters_->Options().surface;
    const Raster<float>& first = bands.front();
    const std::vector<Prepared> prepared =
        Gather(RasterGrid::FromRaster(first).Bounds(), context.spacing, context.ground);
    const std::size_t count = bands.size();
    ForEachAlbedoBlock(bands, context.threads, kBlockRows, [&](std::size_t row0, std::size_t row1) {
        std::vector<const Prepared*> near = NearRows(prepared, first, row0, row1, surface);
        return AlbedoCellVisitor([this, near = std::move(near), &surface,
                                  count](AlbedoSample& value, double x, double y) {
            double excess = 0.0;
            double change = 0.0;
            for (const Prepared* p : near) {
                if (!surface.IsBody() && std::fabs(x - p->crater.x) > p->reach) {
                    continue;
                }
                excess += p->Excess(options_, surface, x, y);
                change += p->Change(options_.texture, surface, x, y);
            }
            Brighten(value, count, excess, change, options_);
        });
    });
}

}  // namespace terraingen
