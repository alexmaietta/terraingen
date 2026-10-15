#include "terraingen/albedo/crater_albedo.h"

#include "terraingen/noise/kernels/hash.h"
#include "terraingen/noise/kernels/value.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/parallel.h"

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
    return 1000.0 * lengthCoefficient * std::pow(0.0005 * diameter, lengthExponent);
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
    if (!(options.minDiameter >= 0.0 && options.minDiameter <= options.maxDiameter &&
          options.minAgeGyr >= 0.0 && options.minAgeGyr <= options.maxAgeGyr)) {
        throw std::invalid_argument("Crater albedo size and age ranges must be ordered and >= 0");
    }
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
struct Ray {
    double cos = 1.0;
    double sin = 0.0;
    double length = 0.0;
    double sinHalfWidth = 0.0;
    std::uint32_t seed = 0;
};

/// A crater's bright material, ready to evaluate at points.
struct CraterAlbedo::Prepared {
    Crater crater;
    CraterOutline outline;
    double weight = 0.0;
    double reach = 0.0;
    std::vector<Ray> rays;
};

CraterAlbedo::CraterAlbedo(std::shared_ptr<const CraterLayer> craters, CraterAlbedoOptions options)
    : craters_(std::move(craters)), options_(options) {
    if (!craters_) {
        throw std::invalid_argument("Crater albedo needs a crater layer");
    }
    ValidateCraterAlbedo(options_);
}

CraterAlbedo::Prepared CraterAlbedo::Prepare(const Crater& crater, double spacing) const {
    Prepared prepared;
    prepared.crater = crater;
    const CraterAlbedoOptions& o = options_;
    if ((crater.manual && !o.manual) || crater.diameter < o.minDiameter ||
        crater.diameter > o.maxDiameter || crater.ageGyr < o.minAgeGyr ||
        crater.ageGyr > o.maxAgeGyr) {
        return prepared;
    }
    RandomStream random(o.seed, kCraterAlbedoStream, static_cast<std::uint32_t>(crater.id),
                        static_cast<std::uint32_t>(crater.id >> 32));
    if (random.NextDouble() >= o.share) {
        return prepared;
    }
    const double left = 1.0 - 1000.0 * crater.ageGyr / o.retention.Myr(crater.diameter);
    const double resolution = ResolutionWeight(crater.diameter, spacing, o.fade);
    if (left <= 0.0 || resolution == 0.0) {
        return prepared;
    }
    prepared.weight = std::pow(left, o.fadeExponent) * resolution;
    prepared.outline = OutlineOf(crater, craters_->Options().apply.irregularity);
    const double radius = 0.5 * crater.diameter;
    prepared.reach = o.ejectaReachRadii * radius * prepared.outline.MaxScale();
    if (!o.rays.enabled) {
        return prepared;
    }
    const double c1 = random.NextDouble();
    const double c2 = random.NextDouble();
    const auto count = static_cast<int>(std::lround(o.rays.count.Sample(c1, c2)));
    const double median = o.rays.MedianLength(crater.diameter);
    const ObliqueImpacts& oblique = o.rays.oblique;
    const CraterImpact impact = Impact(crater);
    const double angle = impact.angleDegrees;
    const double heading = impact.heading;
    const double asymmetry = (impact.speedRatio - 1.0) / (impact.speedRatio + 1.0);
    const double zone = oblique.zoneHalfWidthDegrees * std::numbers::pi / 180.0;
    for (int i = 0; i < count; ++i) {
        const double azimuth = 2.0 * std::numbers::pi * random.NextDouble();
        const double l1 = random.NextDouble();
        const double l2 = random.NextDouble();
        const double w1 = random.NextDouble();
        const double w2 = random.NextDouble();
        Ray ray;
        ray.cos = std::cos(azimuth);
        ray.sin = std::sin(azimuth);
        ray.length = median * std::exp(o.rays.lengthSpread.Sample(l1, l2));
        const double fromDownrange = std::remainder(azimuth - heading, 2.0 * std::numbers::pi);
        if (asymmetry > 0.0) {
            const double speed = 1.0 + asymmetry * std::cos(fromDownrange);
            ray.length *= speed * speed / (1.0 + 0.5 * asymmetry * asymmetry);
        }
        const bool uprange = std::fabs(fromDownrange) > std::numbers::pi - zone;
        const bool downrange = std::fabs(fromDownrange) < zone;
        if ((oblique.enabled && angle < oblique.forbiddenDegrees && uprange) ||
            (oblique.enabled && angle < oblique.butterflyDegrees && downrange)) {
            ray.length = 0.0;
        }
        ray.sinHalfWidth =
            std::sin(0.5 * o.rays.widthDegrees.Sample(w1, w2) * std::numbers::pi / 180.0);
        ray.seed = random.NextU32();
        if (ray.length > radius) {
            prepared.reach = std::fmax(prepared.reach, ray.length);
            prepared.rays.push_back(ray);
        }
    }
    return prepared;
}

namespace {

double PreparedExcess(const CraterAlbedoOptions& o, const Surface& surface, const Crater& crater,
                      const CraterOutline& outline, double weight, const std::vector<Ray>& rays,
                      double x, double y) {
    if (weight == 0.0) {
        return 0.0;
    }
    const double radius = 0.5 * crater.diameter;
    const double radii = CraterProfileDistance(crater, outline, x, y, surface) / radius;
    double excess = 0.0;
    if (radii < 1.0) {
        return weight * o.interiorExcess;
    }
    if (radii < o.ejectaReachRadii) {
        const double edge = std::pow(o.ejectaReachRadii, -o.ejectaExponent);
        excess = o.ejectaExcess * (std::pow(radii, -o.ejectaExponent) - edge) / (1.0 - edge);
    }
    const Vec2d offset = surface.Offset({crater.x, crater.y}, {x, y});
    const double dx = offset.x;
    const double dy = offset.y;
    for (const Ray& ray : rays) {
        const double along = dx * ray.cos + dy * ray.sin;
        if (along <= radius || along >= ray.length) {
            continue;
        }
        const double lateral = std::fabs(dy * ray.cos - dx * ray.sin);
        const double half = std::fmin(along, 2.0 * radius) * ray.sinHalfWidth;
        if (lateral >= half) {
            continue;
        }
        const double q = 1.0 - (lateral / half) * (lateral / half);
        double strength = o.rays.excess * q * q * (ray.length - along) / (ray.length - radius);
        if (o.rays.patchiness > 0.0) {
            const double wavelength = o.rays.patchRadii * radius;
            const double u = along / wavelength;
            const double iu = std::floor(u);
            const float noise = ValueNoise2(static_cast<std::int32_t>(iu), 0,
                                            static_cast<float>(u - iu), 0.5f, ray.seed);
            strength *= 1.0 - o.rays.patchiness * (0.5 + 0.5 * static_cast<double>(noise));
        }
        excess += strength;
    }
    return weight * excess;
}

}  // namespace

double CraterAlbedo::Excess(const Crater& crater, double x, double y, double spacing) const {
    const Prepared prepared = Prepare(crater, spacing);
    return PreparedExcess(options_, craters_->Options().surface, crater, prepared.outline,
                          prepared.weight, prepared.rays, x, y);
}

CraterImpact CraterAlbedo::Impact(const Crater& crater) const {
    const ObliqueImpacts& oblique = options_.rays.oblique;
    CraterImpact impact;
    if (!oblique.enabled) {
        return impact;
    }
    RandomStream random(options_.seed, kObliqueStream, static_cast<std::uint32_t>(crater.id),
                        static_cast<std::uint32_t>(crater.id >> 32));
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
    const std::vector<Prepared> prepared = Gather({x, y, x, y}, context.spacing);
    double excess = 0.0;
    for (const Prepared& p : prepared) {
        excess += PreparedExcess(options_, craters_->Options().surface, p.crater, p.outline,
                                 p.weight, p.rays, x, y);
    }
    Brighten(value, bands, excess, options_);
}

void CraterAlbedo::Apply(std::vector<Raster<float>>& bands, const AlbedoContext& context) const {
    ValidateAlbedoContext(context);
    const Surface& surface = craters_->Options().surface;
    if (bands.empty()) {
        return;
    }
    const Raster<float>& first = bands.front();
    const RasterGrid grid = RasterGrid::Of(first);
    const Bounds2d bounds = grid.Bounds();
    const std::vector<Prepared> prepared = Gather(bounds, context.spacing);
    const std::size_t blocks = (first.Height() + kBlockRows - 1) / kBlockRows;
    ParallelFor(blocks, context.threads, [&](std::size_t block) {
        const std::size_t row0 = block * kBlockRows;
        const std::size_t row1 = std::min(first.Height(), row0 + kBlockRows);
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
        for (std::size_t row = row0; row < row1; ++row) {
            for (std::size_t col = 0; col < first.Width(); ++col) {
                const Vec2d centre = first.CellCenter(col, row);
                double excess = 0.0;
                for (const Prepared* p : near) {
                    if (!surface.IsSphere() && std::fabs(centre.x - p->crater.x) > p->reach) {
                        continue;
                    }
                    excess += PreparedExcess(options_, craters_->Options().surface, p->crater,
                                             p->outline, p->weight, p->rays, centre.x, centre.y);
                }
                AlbedoSample value{};
                for (std::size_t band = 0; band < bands.size(); ++band) {
                    value[band] = bands[band](col, row);
                }
                Brighten(value, bands.size(), excess, options_);
                for (std::size_t band = 0; band < bands.size(); ++band) {
                    bands[band](col, row) = value[band];
                }
            }
        }
    });
}

}  // namespace terraingen
