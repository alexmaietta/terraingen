#include "terraingen/albedo/crater_albedo.h"

#include "terraingen/layers/craters/secondaries.h"
#include "terraingen/noise/fractal.h"
#include "terraingen/noise/hash.h"
#include "terraingen/noise/value.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/log.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

constexpr std::uint32_t kCraterAlbedoStream = 0x414c4244u;
constexpr std::uint32_t kObliqueStream = 0x4f424c51u;
constexpr std::uint32_t kZoneStream = 0x5a4f4e45u;
constexpr std::size_t kBlockRows = 32;

}  // namespace

double RayRetention::Myr(double diameter) const {
    double myr = largeMyr;
    if (diameter <= smallDiameter) {
        myr = smallMyr;
    } else if (diameter < midDiameter) {
        const double t = std::log(diameter / smallDiameter) / std::log(midDiameter / smallDiameter);
        myr = smallMyr * std::pow(midMyr / smallMyr, t);
    } else if (diameter <= largeDiameter) {
        myr = midMyr;
    }
    return scale * myr;
}

void ValidateRayRetention(const RayRetention& retention) {
    if (!(IsPositive(retention.smallDiameter) && IsPositive(retention.midDiameter) &&
          IsPositive(retention.largeDiameter) && retention.smallDiameter < retention.midDiameter &&
          retention.midDiameter <= retention.largeDiameter)) {
        throw std::invalid_argument("Ray retention diameters must be positive and increasing");
    }
    if (!(IsPositive(retention.smallMyr) && IsPositive(retention.midMyr) &&
          IsPositive(retention.largeMyr) && IsPositive(retention.scale))) {
        throw std::invalid_argument("Ray retention times and their scale must be positive");
    }
}

double ObliqueImpacts::MaxStretch() const {
    if (!enabled) {
        return 1.0;
    }
    const double ratio = downrangeSpeedRatio.Highest();
    const double a = (ratio - 1.0) / (ratio + 1.0);
    return (1.0 + a) * (1.0 + a) / (1.0 + 0.5 * a * a);
}

void ValidateObliqueImpacts(const ObliqueImpacts& oblique) {
    ValidateValueDistribution(oblique.downrangeSpeedRatio);
    if (oblique.downrangeSpeedRatio.Lowest() < 1.0 ||
        !std::isfinite(oblique.downrangeSpeedRatio.Highest())) {
        throw std::invalid_argument("The downrange speed ratio must be at least 1 and bounded");
    }
    if (!(oblique.butterflyDegrees >= 0.0 && oblique.butterflyDegrees <= oblique.forbiddenDegrees &&
          oblique.forbiddenDegrees <= oblique.asymmetricDegrees &&
          oblique.asymmetricDegrees <= 90.0)) {
        throw std::invalid_argument(
            "Oblique impact angles must satisfy 0 <= butterfly <= forbidden <= asymmetric <= 90");
    }
    if (!(oblique.zoneHalfWidthDegrees >= 0.0 && oblique.zoneHalfWidthDegrees <= 180.0)) {
        throw std::invalid_argument("A forbidden zone's half width must be in [0, 180] degrees");
    }
    if (oblique.heading && !std::isfinite(*oblique.heading)) {
        throw std::invalid_argument("An impact heading must be finite");
    }
}

double CraterRays::MedianLength(double diameter) const {
    return ElliottMedianRayLength(diameter, lengthCoefficient, lengthExponent);
}

void ValidateCraterRays(const CraterRays& rays) {
    ValidateValueDistribution(rays.count);
    ValidateValueDistribution(rays.lengthSpread);
    ValidateValueDistribution(rays.widthDegrees);
    if (rays.count.Lowest() < 0.0 || rays.count.Highest() > 64.0) {
        throw std::invalid_argument("Craters must have 0 to 64 rays");
    }
    if (!(rays.widthDegrees.Lowest() > 0.0 && rays.widthDegrees.Highest() < 180.0)) {
        throw std::invalid_argument("Ray widths must be in (0, 180) degrees");
    }
    if (!(IsPositive(rays.lengthCoefficient) && IsPositive(rays.lengthExponent))) {
        throw std::invalid_argument("The ray length law needs a positive coefficient and exponent");
    }
    if (!IsNonNegative(rays.excess)) {
        throw std::invalid_argument("The ray maturity excess must not be negative");
    }
    if (!(IsInUnitInterval(rays.patchiness) && IsPositive(rays.patchRadii))) {
        throw std::invalid_argument(
            "Ray patchiness must be in [0, 1] with a positive patch wavelength");
    }
    ValidateObliqueImpacts(rays.oblique);
    if (rays.layout) {
        ValidateRayLayout(*rays.layout);
    }
}

void ValidateCraterAlbedoTexture(const CraterAlbedoTexture& texture) {
    if (!(IsInUnitInterval(texture.streaks) && IsInUnitInterval(texture.floorShare))) {
        throw std::invalid_argument("Crater albedo streaks and floor share must be in [0, 1]");
    }
    if (!(texture.lobes >= 0.0 && texture.lobes <= 0.9 && texture.lobeCount >= 1 &&
          texture.lobeCount <= 64)) {
        throw std::invalid_argument(
            "Crater albedo lobes must be in [0, 0.9] with a lobe count of 1 to 64");
    }
    RequirePositive(texture.streakWidthDegrees, "Crater albedo streak width");
    RequirePositive(texture.streakLengthRadii, "Crater albedo streak length");
    RequireNonNegative(texture.continuousRadii, "Crater albedo continuous ejecta reach");
}

void ValidateReflectanceZone(const ReflectanceZone& zone) {
    RequireUnitInterval(zone.share, "The share of craters with a reflectance zone");
    ValidateValueDistribution(zone.change);
    ValidateValueDistribution(zone.scale);
    if (!(zone.change.Lowest() > -1.0 && std::isfinite(zone.change.Highest()))) {
        throw std::invalid_argument("A reflectance zone's change must be above -1 and bounded");
    }
    if (!(IsPositive(zone.coefficient) && IsPositive(zone.exponent) && zone.scale.Lowest() > 0.0 &&
          std::isfinite(zone.scale.Highest()))) {
        throw std::invalid_argument(
            "A reflectance zone needs a positive reach law and a positive, bounded reach scale");
    }
    if (!(zone.raggedness >= 0.0 && zone.raggedness < 1.0 && zone.feather > 0.0 &&
          zone.feather <= 1.0)) {
        throw std::invalid_argument(
            "A reflectance zone's raggedness must be in [0, 1) and its feather in (0, 1]");
    }
    RequirePositive(zone.fingerWidthDegrees, "A reflectance zone's finger width");
    if (zone.retentionMyr) {
        RequirePositive(*zone.retentionMyr, "A reflectance zone's retention time");
    }
}

double CraterReflectanceZones::MaxReach(double diameter) const {
    double reach = 0.0;
    for (const ReflectanceZone* zone : {&proximalHigh, &proximalLow, &distalHigh, &distalLow}) {
        reach = std::fmax(reach, zone->coefficient * std::pow(diameter, zone->exponent) *
                                     zone->scale.Highest());
    }
    return reach;
}

void ValidateCraterReflectanceZones(const CraterReflectanceZones& zones) {
    for (const ReflectanceZone* zone :
         {&zones.proximalHigh, &zones.proximalLow, &zones.distalHigh, &zones.distalLow}) {
        ValidateReflectanceZone(*zone);
    }
    RequirePositive(zones.maxDiameter, "The largest crater with reflectance zones");
    RequirePositive(zones.excavationRatio, "The excavation depth ratio");
    RequireNonNegative(zones.matureDepth, "The mature regolith depth");
    const ZoneShadows& s = zones.shadows;
    if (!(s.jetAngleDegrees > 0.0 && s.jetAngleDegrees < 90.0 && IsPositive(s.depth) &&
          s.azimuths >= 8 && s.azimuths <= 8192 && s.radialSamples >= 8 &&
          s.radialSamples <= 8192)) {
        throw std::invalid_argument("Zone shadows need a jet angle in (0, 90) degrees, a positive "
                                    "depth and 8 to 8192 azimuths and samples");
    }
}

void ValidateCraterAlbedo(const CraterAlbedoOptions& options) {
    ValidateCraterReflectanceZones(options.zones);
    ValidateCraterAlbedoTexture(options.texture);
    ValidateCraterRays(options.rays);
    ValidateRayRetention(options.retention);
    ValidateResolutionFade(options.fade);
    ValidateCraterSelection(options.selection, "Crater albedo");
    RequireUnitInterval(options.share, "The share of craters with bright material");
    if (!(IsPositive(options.backgroundMaturity) && std::isfinite(options.maxMaturity) &&
          options.maxMaturity >= options.backgroundMaturity)) {
        throw std::invalid_argument(
            "The background maturity must be positive and the cap at least as high");
    }
    if (!(IsNonNegative(options.interiorExcess) && IsNonNegative(options.ejectaExcess))) {
        throw std::invalid_argument("Crater maturity excesses must not be negative");
    }
    if (!(IsPositive(options.ejectaExponent) && std::isfinite(options.ejectaReachRadii) &&
          options.ejectaReachRadii > 1.0 && IsPositive(options.fadeExponent) &&
          std::isfinite(options.originReflectance))) {
        throw std::invalid_argument("Crater albedo needs a positive ejecta exponent and fade "
                                    "exponent and a reach over 1 radius");
    }
}

/// The key of `crater`'s texture.
std::uint32_t TextureKey(const Crater& crater, const CraterAlbedoTexture& texture) {
    return PcgHash(static_cast<std::uint32_t>(crater.id) ^
                   PcgHash(static_cast<std::uint32_t>(crater.id >> 32) ^ texture.seed));
}

/// How far `crater`'s ejecta edge reaches at `angle` radians around it, over its mean reach.
double LobeScale(double angle, std::uint32_t key, const CraterAlbedoTexture& texture) {
    if (texture.lobes == 0.0) {
        return 1.0;
    }
    FractalParams params;
    params.seed = key ^ 0x4C4F4245u;
    params.wavelength = 1.0f;
    params.amplitude = 1.0f;
    params.octaves = 2;
    const double ring = static_cast<double>(texture.lobeCount) / (2.0 * std::numbers::pi);
    const double n =
        static_cast<double>(Fractal3(ring * std::cos(angle), ring * std::sin(angle), 0.0, params));
    return 1.0 + texture.lobes * std::clamp(n, -1.0, 1.0);
}

/// The share of the excess the filaments leave at `radii` radii and `angle` radians around a
/// crater: 1 along a filament, down to 1 - `streaks` between them.
double StreakShare(double radii, double angle, std::uint32_t key,
                   const CraterAlbedoTexture& texture) {
    if (texture.streaks == 0.0) {
        return 1.0;
    }
    FractalParams params;
    params.seed = key;
    params.wavelength = 1.0f;
    params.amplitude = 1.0f;
    params.octaves = 3;
    const double ring = 180.0 / (std::numbers::pi * texture.streakWidthDegrees);
    const double n = static_cast<double>(Fractal3(ring * std::cos(angle), ring * std::sin(angle),
                                                  radii / texture.streakLengthRadii, params));
    const double filament = Smoothstep(std::clamp(0.5 + 1.5 * n, 0.0, 1.0));
    return 1.0 - texture.streaks * (1.0 - filament);
}

/// How far a jet from a crater's rim runs above the ground, on rays around the crater.
struct ShadowProfile {
    std::size_t azimuths = 0;
    std::size_t samples = 0;
    double start = 0.0;
    double step = 0.0;
    double depth = 1.0;
    std::vector<float> gap;

    /// How deep in shadow the ground is at `distance` metres and `angle` radians from the
    /// crater's centre, from 0 in the open to 1 in full shadow.
    double Shade(double distance, double angle) const {
        if (distance <= start) {
            return 0.0;
        }
        const auto rings = static_cast<double>(azimuths);
        const double r = std::fmin((distance - start) / step, static_cast<double>(samples - 1));
        double a = angle / (2.0 * std::numbers::pi) * rings;
        a -= std::floor(a / rings) * rings;
        const std::size_t a0 = std::min(static_cast<std::size_t>(a), azimuths - 1);
        const std::size_t a1 = (a0 + 1) % azimuths;
        const std::size_t r0 = std::min(static_cast<std::size_t>(r), samples - 1);
        const std::size_t r1 = std::min(r0 + 1, samples - 1);
        const double fa = a - static_cast<double>(a0);
        const double fr = r - static_cast<double>(r0);
        const auto at = [&](std::size_t i, std::size_t j) {
            return static_cast<double>(gap[i * samples + j]);
        };
        const double g = (1.0 - fa) * ((1.0 - fr) * at(a0, r0) + fr * at(a0, r1)) +
                         fa * ((1.0 - fr) * at(a1, r0) + fr * at(a1, r1));
        return Smoothstep(std::clamp(g / depth, 0.0, 1.0));
    }
};

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

    /// How much of the change the zone gives at `distance` metres and `angle` radians from the
    /// crater's centre, from 1 inside to 0 beyond its ragged edge, whose fingers bend and break
    /// with distance.
    double Cover(double distance, double angle) const {
        if (distance >= reach) {
            return 0.0;
        }
        double finger = 1.0;
        if (raggedness > 0.0) {
            FractalParams params;
            params.seed = key;
            params.wavelength = 1.0f;
            params.amplitude = 1.0f;
            params.octaves = 4;
            const double n = static_cast<double>(Fractal3(
                ring * std::cos(angle), ring * std::sin(angle), 3.0 * distance / reach, params));
            finger = 1.0 - raggedness * std::clamp(0.5 - n, 0.0, 1.0);
        }
        const double t = distance / (reach * finger);
        return 1.0 - Smoothstep(std::clamp((t - (1.0 - feather)) / feather, 0.0, 1.0));
    }
};

/// One ray, in the frame of its crater.
struct CraterAlbedo::Ray {
    double cos = 1.0;
    double sin = 0.0;
    double length = 0.0;
    double sinHalfWidth = 0.0;
    std::uint32_t seed = 0;

    /// The ray's excess at `along` metres from the crater's centre along it and `lateral` across,
    /// for a crater of `radius` metres.
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
    double weight = 0.0;
    double reach = 0.0;
    std::vector<Ray> rays;
    std::vector<DrawnZone> zones;
    std::shared_ptr<const ShadowProfile> shadow;

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
        double change = 0.0;
        double open = 1.0;
        for (const DrawnZone& zone : zones) {
            const double cover = zone.Cover(distance, angle) * (zone.distal ? 1.0 - shade : 1.0);
            const double streak = zone.streaked
                                      ? StreakShare(2.0 * distance / crater.diameter, angle,
                                                    TextureKey(crater, texture), texture)
                                      : 1.0;
            change += open * cover * zone.strength * zone.resolution * streak * zone.change;
            open *= 1.0 - cover * zone.strength;
        }
        return change;
    }

    /// The excess optical maturity at (`x`, `y`).
    double Excess(const CraterAlbedoOptions& o, const Surface& surface, double x, double y) const {
        if (weight == 0.0) {
            return 0.0;
        }
        const double radius = 0.5 * crater.diameter;
        const double radii = CraterProfileDistance(crater, outline, x, y, surface) / radius;
        const CraterAlbedoTexture& texture = o.texture;
        const bool textured = texture.streaks > 0.0 || texture.lobes > 0.0;
        const Vec2d offset = surface.Offset({crater.x, crater.y}, {x, y});
        const double angle = textured ? std::atan2(offset.y, offset.x) : 0.0;
        const std::uint32_t key = textured ? TextureKey(crater, texture) : 0u;
        const double streak = StreakShare(radii, angle, key, texture);
        if (radii < 1.0) {
            double interior = o.interiorExcess;
            const double wall = Smoothstep(std::clamp((radii - 0.3) / 0.6, 0.0, 1.0));
            if (texture.floorShare < 1.0) {
                interior *= texture.floorShare + (1.0 - texture.floorShare) * wall;
            }
            return weight * interior * (1.0 - (1.0 - streak) * wall);
        }
        double excess = 0.0;
        const double lobed = radii / LobeScale(angle, key, texture);
        if (lobed < o.ejectaReachRadii) {
            const double edge = std::pow(o.ejectaReachRadii, -o.ejectaExponent);
            excess = o.ejectaExcess * (std::pow(std::fmax(lobed, 1.0), -o.ejectaExponent) - edge) /
                     (1.0 - edge);
        }
        for (const Ray& ray : rays) {
            const double along = offset.x * ray.cos + offset.y * ray.sin;
            const double lateral = std::fabs(offset.y * ray.cos - offset.x * ray.sin);
            excess += ray.ExcessAt(along, lateral, radius, o.rays);
        }
        if (texture.continuousRadii > 0.0) {
            const double ramp =
                Smoothstep(std::clamp((radii - 1.0) / texture.continuousRadii, 0.0, 1.0));
            return weight * excess * (1.0 - (1.0 - streak) * ramp);
        }
        return weight * excess * streak;
    }
};

CraterAlbedo::CraterAlbedo(std::shared_ptr<const CraterLayer> craters, CraterAlbedoOptions options)
    : craters_(std::move(craters)), options_(options) {
    if (!craters_) {
        throw std::invalid_argument("Crater albedo needs a crater layer");
    }
    ValidateCraterAlbedo(options_);
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
    const double zone = oblique.zoneHalfWidthDegrees * std::numbers::pi / 180.0;
    const double drawn = 2.0 * std::numbers::pi * random.NextDouble();
    const double azimuth = direction != nullptr ? direction->azimuth : drawn;
    const double l1 = random.NextDouble();
    const double l2 = random.NextDouble();
    const double w1 = random.NextDouble();
    const double w2 = random.NextDouble();
    Ray ray;
    ray.cos = std::cos(azimuth);
    ray.sin = std::sin(azimuth);
    ray.length = median * std::exp(rays.lengthSpread.Sample(l1, l2));
    const double fromDownrange = std::remainder(azimuth - impact.heading, 2.0 * std::numbers::pi);
    if (asymmetry > 0.0) {
        const double speed = 1.0 + asymmetry * std::cos(fromDownrange);
        ray.length *= speed * speed / (1.0 + 0.5 * asymmetry * asymmetry);
    }
    const bool uprange = std::fabs(fromDownrange) > std::numbers::pi - zone;
    const bool downrange = std::fabs(fromDownrange) < zone;
    if ((oblique.enabled && impact.angleDegrees < oblique.forbiddenDegrees && uprange) ||
        (oblique.enabled && impact.angleDegrees < oblique.butterflyDegrees && downrange)) {
        ray.length = 0.0;
    }
    ray.sinHalfWidth =
        direction != nullptr
            ? direction->sinHalfWidth
            : std::sin(0.5 * rays.widthDegrees.Sample(w1, w2) * std::numbers::pi / 180.0);
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
    const double diameter = prepared.crater.diameter;
    const IdWords id = SplitId(prepared.crater.id);
    RandomStream random(z.seed, kZoneStream, id.low, id.high);
    const bool immature = z.excavationRatio * diameter > z.matureDepth;
    std::vector<DrawnZone> distal;
    for (const ReflectanceZone* zone :
         {&z.proximalHigh, &z.proximalLow, &z.distalHigh, &z.distalLow}) {
        const double present = random.NextDouble();
        const double c1 = random.NextDouble();
        const double c2 = random.NextDouble();
        const double s1 = random.NextDouble();
        const double s2 = random.NextDouble();
        DrawnZone drawn;
        drawn.key = random.NextU32();
        const double life =
            zone->retentionMyr
                ? std::fmin(left, 1.0 - 1000.0 * prepared.crater.ageGyr / *zone->retentionMyr)
                : left;
        if (present >= zone->share || (zone == &z.proximalHigh && !immature) || life <= 0.0) {
            continue;
        }
        drawn.change = zone->change.Sample(c1, c2);
        drawn.strength = std::pow(life, options_.fadeExponent);
        drawn.resolution = resolution;
        drawn.reach =
            zone->coefficient * std::pow(diameter, zone->exponent) * zone->scale.Sample(s1, s2);
        drawn.raggedness = zone->raggedness;
        drawn.ring = 180.0 / (std::numbers::pi * zone->fingerWidthDegrees);
        drawn.feather = zone->feather;
        drawn.streaked = zone == &z.proximalHigh;
        drawn.distal = zone == &z.distalHigh || zone == &z.distalLow;
        prepared.reach = std::fmax(prepared.reach, drawn.reach);
        (zone == &z.distalHigh || zone == &z.distalLow ? distal : prepared.zones).push_back(drawn);
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
    const double left = 1.0 - 1000.0 * crater.ageGyr / o.retention.Myr(crater.diameter);
    const double resolution = ResolutionWeight(crater.diameter, spacing, o.fade);
    if (left <= 0.0 || resolution == 0.0) {
        return prepared;
    }
    prepared.weight = std::pow(left, o.fadeExponent) * resolution;
    prepared.outline = OutlineOf(crater, craters_->Options().apply.irregularity);
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
        AddShadows(prepared, *ground);
    }
    return prepared.Change(options_.texture, craters_->Options().surface, x, y);
}

void CraterAlbedo::AddShadows(Prepared& prepared, const Ground& ground) const {
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
    auto profile = std::make_shared<ShadowProfile>();
    const auto azimuths = static_cast<std::size_t>(s.azimuths);
    const auto samples = static_cast<std::size_t>(s.radialSamples);
    profile->azimuths = azimuths;
    profile->samples = samples;
    profile->start = radius;
    profile->step = (reach - radius) / static_cast<double>(samples - 1);
    profile->depth = s.depth;
    const Surface& surface = craters_->Options().surface;
    const std::size_t count = azimuths * samples;
    std::vector<Vec2d> points;
    points.reserve(count);
    for (std::size_t i = 0; i < azimuths; ++i) {
        const double angle =
            2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(azimuths);
        for (std::size_t j = 0; j < samples; ++j) {
            const double distance = radius + static_cast<double>(j) * profile->step;
            points.push_back(surface.Locate(
                {crater.x, crater.y}, {distance * std::cos(angle), distance * std::sin(angle)}));
        }
    }
    std::vector<float> heights(count);
    ground.HeightsAt(points, heights, profile->step);
    const double drop = std::tan(s.jetAngleDegrees * std::numbers::pi / 180.0) * profile->step;
    profile->gap.assign(count, 0.0f);
    for (std::size_t i = 0; i < azimuths; ++i) {
        const std::size_t row = i * samples;
        double jet = static_cast<double>(heights[row]);
        for (std::size_t j = 1; j < samples; ++j) {
            const double h = static_cast<double>(heights[row + j]);
            if (std::isnan(h) || std::isnan(jet)) {
                jet = h;
                continue;
            }
            jet = std::fmax(h, jet - drop);
            profile->gap[row + j] = static_cast<float>(jet - h);
        }
    }
    prepared.shadow = std::move(profile);
}

CraterImpact CraterAlbedo::Impact(const Crater& crater) const {
    const ObliqueImpacts& oblique = options_.rays.oblique;
    CraterImpact impact;
    if (!oblique.enabled) {
        return impact;
    }
    const IdWords id = SplitId(crater.id);
    RandomStream random(options_.seed, kObliqueStream, id.low, id.high);
    impact.angleDegrees = std::asin(std::sqrt(random.NextDouble())) * 180.0 / std::numbers::pi;
    const double h = random.NextDouble();
    impact.heading = oblique.heading ? *oblique.heading : 2.0 * std::numbers::pi * h;
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

std::vector<CraterAlbedo::Prepared> CraterAlbedo::Gather(const Bounds2d& region, double spacing,
                                                         const Ground* ground) const {
    const double largest = craters_->MaxFeatureDiameter();
    const double rayReach = options_.rays.enabled
                                ? options_.rays.MedianLength(largest) *
                                      std::exp(options_.rays.lengthSpread.Highest()) *
                                      options_.rays.oblique.MaxStretch()
                                : 0.0;
    const double ejectaReach =
        std::fmax(rayReach, 0.5 * largest * options_.ejectaReachRadii *
                                MaxOutlineScale(craters_->Options().apply.irregularity) *
                                (1.0 + options_.texture.lobes));
    const Surface& surface = craters_->Options().surface;
    std::vector<Prepared> prepared;
    const auto take = [&](const CraterList& list) {
        for (std::size_t i = 0; i < list.Size(); ++i) {
            Prepared one = Prepare(list.At(i), spacing);
            if (one.weight == 0.0) {
                continue;
            }
            const Crater& c = one.crater;
            if (!surface.Contains(surface.Expand(region, one.reach), {c.x, c.y})) {
                continue;
            }
            prepared.push_back(std::move(one));
        }
    };
    take(craters_->Craters(surface.Expand(region, ejectaReach), spacing));
    if (options_.zones.enabled) {
        for (double high = std::fmin(largest, options_.zones.maxDiameter);
             options_.zones.MaxReach(high) > ejectaReach; high *= 0.5) {
            take(craters_->Craters(surface.Expand(region, options_.zones.MaxReach(high)), spacing,
                                   0.5 * high, high));
        }
    }
    std::sort(prepared.begin(), prepared.end(),
              [](const Prepared& a, const Prepared& b) { return a.crater.id < b.crater.id; });
    prepared.erase(std::unique(prepared.begin(), prepared.end(),
                               [](const Prepared& a, const Prepared& b) {
                                   return a.crater.id == b.crater.id;
                               }),
                   prepared.end());
    if (ground != nullptr && options_.zones.enabled && options_.zones.shadows.enabled) {
        std::size_t shadowed = 0;
        for (Prepared& p : prepared) {
            AddShadows(p, *ground);
            shadowed += p.shadow ? 1u : 0u;
        }
        LogDebug("albedo", "Sampled the ground for the zone shadows of ", shadowed, " craters");
    }
    return prepared;
}

namespace {

/// Moves `value` to the reflectance at the background maturity plus `excess`, capped, then
/// changes it by the relative reflectance `change`, never below 0.
void Brighten(AlbedoSample& value, std::size_t bands, double excess, double change,
              const CraterAlbedoOptions& o) {
    if (excess == 0.0 && change == 0.0) {
        return;
    }
    const double maturity = std::fmin(o.backgroundMaturity + excess, o.maxMaturity);
    const double factor = std::fmax(0.0, 1.0 + change);
    for (std::size_t band = 0; band < bands; ++band) {
        if (std::isnan(value[band])) {
            continue;
        }
        double reflectance = value[band];
        if (excess != 0.0) {
            reflectance = ReflectanceAtMaturity(reflectance, o.backgroundMaturity, maturity,
                                                o.originReflectance);
        }
        value[band] = static_cast<float>(factor * reflectance);
    }
}

}  // namespace

void CraterAlbedo::ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                           const AlbedoContext& context) const {
    ValidateAlbedoContext(context);
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
    ValidateAlbedoContext(context);
    if (bands.empty()) {
        return;
    }
    const Surface& surface = craters_->Options().surface;
    const Raster<float>& first = bands.front();
    const std::vector<Prepared> prepared =
        Gather(RasterGrid::Of(first).Bounds(), context.spacing, context.ground);
    const std::size_t count = bands.size();
    ForEachAlbedoBlock(bands, context.threads, kBlockRows, [&](std::size_t row0, std::size_t row1) {
        const double ya = first.CellCenter(0, row0).y;
        const double yb = first.CellCenter(0, row1 - 1).y;
        const double yMin = std::fmin(ya, yb);
        const double yMax = std::fmax(ya, yb);
        std::vector<const Prepared*> near;
        for (const Prepared& p : prepared) {
            const double reachY = p.reach / surface.UnitLength();
            if (p.crater.y + reachY >= yMin && p.crater.y - reachY <= yMax) {
                near.push_back(&p);
            }
        }
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
