#include "terraingen/albedo/crater_albedo.h"

#include "terraingen/layers/craters/secondaries.h"
#include "terraingen/noise/kernels/value.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

constexpr std::uint32_t kCraterAlbedoStream = 0x414c4244u;
constexpr std::uint32_t kObliqueStream = 0x4f424c51u;
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
}

void ValidateCraterAlbedo(const CraterAlbedoOptions& options) {
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

    /// The excess optical maturity at (`x`, `y`).
    double Excess(const CraterAlbedoOptions& o, const Surface& surface, double x, double y) const {
        if (weight == 0.0) {
            return 0.0;
        }
        const double radius = 0.5 * crater.diameter;
        const double radii = CraterProfileDistance(crater, outline, x, y, surface) / radius;
        if (radii < 1.0) {
            return weight * o.interiorExcess;
        }
        double excess = 0.0;
        if (radii < o.ejectaReachRadii) {
            const double edge = std::pow(o.ejectaReachRadii, -o.ejectaExponent);
            excess = o.ejectaExcess * (std::pow(radii, -o.ejectaExponent) - edge) / (1.0 - edge);
        }
        const Vec2d offset = surface.Offset({crater.x, crater.y}, {x, y});
        for (const Ray& ray : rays) {
            const double along = offset.x * ray.cos + offset.y * ray.sin;
            const double lateral = std::fabs(offset.y * ray.cos - offset.x * ray.sin);
            excess += ray.ExcessAt(along, lateral, radius, o.rays);
        }
        return weight * excess;
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
                                        const CraterImpact& impact) const {
    const CraterRays& rays = options_.rays;
    const ObliqueImpacts& oblique = rays.oblique;
    const double asymmetry = (impact.speedRatio - 1.0) / (impact.speedRatio + 1.0);
    const double zone = oblique.zoneHalfWidthDegrees * std::numbers::pi / 180.0;
    const double azimuth = 2.0 * std::numbers::pi * random.NextDouble();
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
    ray.sinHalfWidth = std::sin(0.5 * rays.widthDegrees.Sample(w1, w2) * std::numbers::pi / 180.0);
    ray.seed = random.NextU32();
    return ray;
}

void CraterAlbedo::AddRays(Prepared& prepared, RandomStream& random) const {
    const Crater& crater = prepared.crater;
    const double c1 = random.NextDouble();
    const double c2 = random.NextDouble();
    const auto count = static_cast<int>(std::lround(options_.rays.count.Sample(c1, c2)));
    const double median = options_.rays.MedianLength(crater.diameter);
    const CraterImpact impact = Impact(crater);
    for (int i = 0; i < count; ++i) {
        const Ray ray = DrawRay(random, median, impact);
        if (ray.length > 0.5 * crater.diameter) {
            prepared.reach = std::fmax(prepared.reach, ray.length);
            prepared.rays.push_back(ray);
        }
    }
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
    prepared.reach = o.ejectaReachRadii * (0.5 * crater.diameter) * prepared.outline.MaxScale();
    if (o.rays.enabled) {
        AddRays(prepared, random);
    }
    return prepared;
}

double CraterAlbedo::Excess(const Crater& crater, double x, double y, double spacing) const {
    return Prepare(crater, spacing).Excess(options_, craters_->Options().surface, x, y);
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

std::vector<CraterAlbedo::Prepared> CraterAlbedo::Gather(const Bounds2d& region,
                                                         double spacing) const {
    const double largest = craters_->MaxFeatureDiameter();
    const double rayReach = options_.rays.enabled
                                ? options_.rays.MedianLength(largest) *
                                      std::exp(options_.rays.lengthSpread.Highest()) *
                                      options_.rays.oblique.MaxStretch()
                                : 0.0;
    const double reach =
        std::fmax(rayReach, 0.5 * largest * options_.ejectaReachRadii *
                                MaxOutlineScale(craters_->Options().apply.irregularity));
    const Surface& surface = craters_->Options().surface;
    const CraterList list = craters_->Craters(surface.Expand(region, reach), spacing);
    std::vector<Prepared> prepared;
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
    std::sort(prepared.begin(), prepared.end(),
              [](const Prepared& a, const Prepared& b) { return a.crater.id < b.crater.id; });
    prepared.erase(std::unique(prepared.begin(), prepared.end(),
                               [](const Prepared& a, const Prepared& b) {
                                   return a.crater.id == b.crater.id;
                               }),
                   prepared.end());
    return prepared;
}

namespace {

void Brighten(AlbedoSample& value, std::size_t bands, double excess, const CraterAlbedoOptions& o) {
    if (excess == 0.0) {
        return;
    }
    const double maturity = std::fmin(o.backgroundMaturity + excess, o.maxMaturity);
    for (std::size_t band = 0; band < bands; ++band) {
        if (std::isnan(value[band])) {
            continue;
        }
        value[band] = static_cast<float>(ReflectanceAtMaturity(value[band], o.backgroundMaturity,
                                                               maturity, o.originReflectance));
    }
}

}  // namespace

void CraterAlbedo::ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                           const AlbedoContext& context) const {
    ValidateAlbedoContext(context);
    const Surface& surface = craters_->Options().surface;
    double excess = 0.0;
    for (const Prepared& p : Gather(PointBounds(x, y), context.spacing)) {
        excess += p.Excess(options_, surface, x, y);
    }
    Brighten(value, bands, excess, options_);
}

void CraterAlbedo::Apply(std::vector<Raster<float>>& bands, const AlbedoContext& context) const {
    ValidateAlbedoContext(context);
    if (bands.empty()) {
        return;
    }
    const Surface& surface = craters_->Options().surface;
    const Raster<float>& first = bands.front();
    const std::vector<Prepared> prepared = Gather(RasterGrid::Of(first).Bounds(), context.spacing);
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
            for (const Prepared* p : near) {
                if (!surface.IsSphere() && std::fabs(x - p->crater.x) > p->reach) {
                    continue;
                }
                excess += p->Excess(options_, surface, x, y);
            }
            Brighten(value, count, excess, options_);
        });
    });
}

}  // namespace terraingen
