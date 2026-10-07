#include "terraingen/layers/craters/secondaries.h"

#include "terraingen/noise/kernels/philox.h"
#include "terraingen/random/samplers.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/grid.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

constexpr std::uint32_t kSecondaryStream = 0x53454344u;

bool Positive(double value) {
    return std::isfinite(value) && value > 0.0;
}

/// The primary's ID mixed with a cell's indices into a secondary's ID.
std::uint64_t SecondaryId(std::uint64_t primary, int band, std::int64_t ring, std::int64_t sector,
                          std::uint32_t index) {
    const U32x4 block = Philox4x32(
        {static_cast<std::uint32_t>(primary), static_cast<std::uint32_t>(primary >> 32),
         static_cast<std::uint32_t>(band) ^ (static_cast<std::uint32_t>(ring) << 8), index},
        {static_cast<std::uint32_t>(sector), kSecondaryStream});
    return (static_cast<std::uint64_t>(block.x) << 32) | block.y;
}

/// The smallest and largest distances, and the directions (radians counter-clockwise from east,
/// as an interval around `mid` of half-width `half`), from `centre` to `region` on `surface`;
/// the whole circle when `region` holds the centre.
struct Footprint {
    double near = 0.0;
    double far = 0.0;
    double mid = 0.0;
    double half = std::numbers::pi;
};

Footprint FootprintOf(const Vec2d& centre, const Bounds2d& region, const Surface& surface) {
    Footprint footprint;
    std::vector<Vec2d> corners;
    for (const double x : {region.minX, region.maxX}) {
        for (const double y : {region.minY, region.maxY}) {
            corners.push_back(surface.Offset(centre, {x, y}));
        }
    }
    double minX = INFINITY;
    double maxX = -INFINITY;
    double minY = INFINITY;
    double maxY = -INFINITY;
    for (const Vec2d& c : corners) {
        minX = std::fmin(minX, c.x);
        maxX = std::fmax(maxX, c.x);
        minY = std::fmin(minY, c.y);
        maxY = std::fmax(maxY, c.y);
        footprint.far = std::fmax(footprint.far, std::hypot(c.x, c.y));
    }
    const double dx = std::fmax(0.0, std::fmax(minX, -maxX));
    const double dy = std::fmax(0.0, std::fmax(minY, -maxY));
    footprint.near = std::hypot(dx, dy);
    if (footprint.near == 0.0) {
        return footprint;
    }
    footprint.mid = std::atan2(0.5 * (minY + maxY), 0.5 * (minX + maxX));
    double half = 0.0;
    for (const Vec2d& c : corners) {
        half = std::fmax(half, std::fabs(std::remainder(std::atan2(c.y, c.x) - footprint.mid,
                                                        2.0 * std::numbers::pi)));
    }
    footprint.half = std::fmin(std::numbers::pi, half);
    return footprint;
}

}  // namespace

double SingerMaxSecondaryDiameter(double diameter, double range) {
    const double d = diameter / 1000.0;
    const double r = range / 1000.0;
    return 1000.0 * 0.022 * std::pow(d, 1.96) * std::pow(r, -0.14 * std::log(d));
}

double SecondaryOnsetRange(double diameter) {
    const double small = 3000.0;
    const double large = 31000.0;
    double rim = 1.8;
    if (diameter >= large) {
        rim = 1.3;
    } else if (diameter > small) {
        const double t = std::log(diameter / small) / std::log(large / small);
        rim = 1.8 + t * (1.3 - 1.8);
    }
    return (0.5 + rim) * diameter;
}

void ValidateSecondaryCraterOptions(const SecondaryCraterOptions& options) {
    if (!(options.onsetCount && Positive(*options.onsetCount))) {
        throw std::invalid_argument("Secondary craters need an onset count: the sources give their "
                                    "sizes, not their number");
    }
    if (!(Positive(options.countFraction) && Positive(options.sizeSlope))) {
        throw std::invalid_argument("The secondary count fraction and size slope must be positive");
    }
    if (!(Positive(options.minDiameter) && options.maxDiameter > options.minDiameter)) {
        throw std::invalid_argument("Secondary diameters must satisfy 0 < min < max");
    }
    if (options.outerRange && !Positive(*options.outerRange)) {
        throw std::invalid_argument("The secondary outer range must be positive");
    }
    if (!(options.minPrimaryDiameter >= 0.0 &&
          options.maxPrimaryDiameter >= options.minPrimaryDiameter && options.minAgeGyr >= 0.0 &&
          options.maxAgeGyr >= options.minAgeGyr)) {
        throw std::invalid_argument("Primary size and age ranges must be ordered and >= 0");
    }
    if (!(Positive(options.cellSizeFactor) && Positive(options.maxExpected))) {
        throw std::invalid_argument("The cell size factor and expected limit must be positive");
    }
}

SecondaryCraterLayer::SecondaryCraterLayer(std::shared_ptr<const CraterLayer> primaries,
                                           SecondaryCraterModels models,
                                           SecondaryCraterOptions options, CraterApplyOptions apply)
    : primaries_(std::move(primaries)), models_(std::move(models)), options_(options),
      apply_(apply) {
    if (!primaries_ || !models_.profile || !models_.degradation) {
        throw std::invalid_argument("Secondary craters need primaries, a profile and degradation");
    }
    ValidateSecondaryCraterOptions(options_);
    ValidateCraterApplyOptions(apply_);
    if (apply_.datum != CraterDatum::None) {
        throw std::invalid_argument("Secondary craters are additive; datums are not supported");
    }
    apply_.surface = primaries_->Options().surface;
}

double SecondaryCraterLayer::OuterRange(double diameter) const {
    if (options_.outerRange) {
        return *options_.outerRange * diameter;
    }
    const double median = 1000.0 * 6.59 * std::pow(0.0005 * diameter, 1.27);
    return 1.6 * median;
}

double SecondaryCraterLayer::MaxFeatureDiameter() const {
    const double largest = primaries_->MaxFeatureDiameter();
    if (!(largest > 0.0)) {
        return 0.0;
    }
    return std::fmin(options_.maxDiameter,
                     SingerMaxSecondaryDiameter(largest, SecondaryOnsetRange(largest)));
}

CraterList SecondaryCraterLayer::Secondaries(const Bounds2d& region, double spacing) const {
    const Surface& surface = apply_.surface;
    const double reachRadii = models_.profile->ReachRadii();
    const double largest = primaries_->MaxFeatureDiameter();
    const double secondaryReach = 0.5 * reachRadii * MaxFeatureDiameter();
    const Bounds2d wanted = surface.Expand(region, secondaryReach);
    const CraterList primaries =
        primaries_->Craters(surface.Expand(wanted, OuterRange(largest)), 0.0);
    const double skipBelow = spacing > 0.0 ? apply_.fade.startSamples * spacing : 0.0;
    const double s = options_.sizeSlope;
    CraterList secondaries;
    double expected = 0.0;
    for (std::size_t n = 0; n < primaries.Size(); ++n) {
        const Crater primary = primaries.At(n);
        const double diameter = primary.diameter;
        if ((primary.manual && !options_.manual) || diameter < options_.minPrimaryDiameter ||
            diameter > options_.maxPrimaryDiameter || primary.ageGyr < options_.minAgeGyr ||
            primary.ageGyr > options_.maxAgeGyr) {
            continue;
        }
        const double onset = SecondaryOnsetRange(diameter);
        const double outer = OuterRange(diameter);
        if (!(outer > onset)) {
            continue;
        }
        const Footprint footprint = FootprintOf({primary.x, primary.y}, wanted, surface);
        const double density0 =
            *options_.onsetCount / (0.25 * std::numbers::pi * diameter * diameter);
        const double reference = options_.countFraction * diameter;
        const double dk = std::log(diameter / 1000.0);
        const double biggest =
            std::fmin(options_.maxDiameter, std::fmax(SingerMaxSecondaryDiameter(diameter, onset),
                                                      SingerMaxSecondaryDiameter(diameter, outer)));
        const int firstBand = std::ilogb(std::fmax(options_.minDiameter, 1e-9));
        const int lastBand = std::ilogb(biggest);
        for (int band = firstBand; band <= lastBand; ++band) {
            const double low = std::fmax(options_.minDiameter, std::ldexp(1.0, band));
            const double high = std::fmin(options_.maxDiameter, std::ldexp(1.0, band + 1));
            if (!(high > low) || high <= skipBelow) {
                continue;
            }
            double far = outer;
            if (dk > 0.0) {
                const double a = 0.022 * std::pow(diameter / 1000.0, 1.96);
                far = std::fmin(outer, 1000.0 * std::pow(a / (low / 1000.0), 1.0 / (0.14 * dk)));
            }
            const double cell = options_.cellSizeFactor * high;
            const double inner = std::fmax(onset, footprint.near - cell);
            const double outerEdge = std::fmin(far, footprint.far + cell);
            if (!(outerEdge > inner)) {
                continue;
            }
            const auto firstRing = static_cast<std::int64_t>(std::floor((inner - onset) / cell));
            const auto lastRing = static_cast<std::int64_t>(std::ceil((outerEdge - onset) / cell));
            const double bandShare = std::pow(low / reference, -s) - std::pow(high / reference, -s);
            for (std::int64_t ring = firstRing; ring < lastRing; ++ring) {
                const double r0 = onset + static_cast<double>(ring) * cell;
                const double r1 = std::fmin(far, r0 + cell);
                if (!(r1 > r0)) {
                    continue;
                }
                const auto sectors = std::max<std::int64_t>(
                    1, static_cast<std::int64_t>(
                           std::ceil(2.0 * std::numbers::pi * 0.5 * (r0 + r1) / cell)));
                const double width = 2.0 * std::numbers::pi / static_cast<double>(sectors);
                std::int64_t s0 = 0;
                std::int64_t s1 = sectors;
                if (footprint.half < std::numbers::pi) {
                    const double margin = cell / r0 + width;
                    const double from = footprint.mid - footprint.half - margin;
                    const double to = footprint.mid + footprint.half + margin;
                    if (to - from < 2.0 * std::numbers::pi) {
                        s0 = static_cast<std::int64_t>(std::floor(from / width));
                        s1 = static_cast<std::int64_t>(std::ceil(to / width));
                    }
                }
                const double r = 1.0 + (r0 - onset) / diameter;
                const double area = 0.5 * width * (r1 * r1 - r0 * r0);
                const double mean = density0 / (r * r) * bandShare * area;
                for (std::int64_t raw = s0; raw < s1; ++raw) {
                    const std::int64_t sector = ((raw % sectors) + sectors) % sectors;
                    expected += mean;
                    if (expected > options_.maxExpected) {
                        throw std::invalid_argument("Expected secondary count exceeds maxExpected");
                    }
                    RandomStream random(options_.seed, kSecondaryStream,
                                        static_cast<std::uint32_t>(primary.id) ^
                                            static_cast<std::uint32_t>(primary.id >> 32),
                                        static_cast<std::uint32_t>(band) ^
                                            (static_cast<std::uint32_t>(ring) << 8),
                                        static_cast<std::uint32_t>(sector));
                    const std::uint64_t count = SamplePoisson(random, mean);
                    for (std::uint64_t index = 0; index < count; ++index) {
                        const double range =
                            std::sqrt(r0 * r0 + random.NextDouble() * (r1 * r1 - r0 * r0));
                        const double azimuth =
                            (static_cast<double>(sector) + random.NextDouble()) * width;
                        const double u = random.NextDouble();
                        const double size =
                            reference *
                            std::pow(std::pow(low / reference, -s) - u * bandShare, -1.0 / s);
                        const double keep = random.NextDouble();
                        const double here = 1.0 + (range - onset) / diameter;
                        if (keep >= (r * r) / (here * here) ||
                            size >= SingerMaxSecondaryDiameter(diameter, range)) {
                            continue;
                        }
                        const Vec2d at =
                            surface.Locate({primary.x, primary.y},
                                           {range * std::cos(azimuth), range * std::sin(azimuth)});
                        const double margin = 0.5 * reachRadii * size;
                        if (!surface.Contains(surface.Expand(region, margin), at)) {
                            continue;
                        }
                        secondaries.Append({at.x, at.y, size, primary.ageGyr,
                                            SecondaryId(primary.id, band, ring, sector,
                                                        static_cast<std::uint32_t>(index))});
                    }
                }
            }
        }
    }
    return secondaries;
}

void SecondaryCraterLayer::Apply(Raster<float>& heights, const LayerContext& context,
                                 TerrainFeatures& features) const {
    const RasterGrid grid = RasterGrid::Of(heights);
    const double resolved = std::max(context.spacing, grid.Spacing(apply_.surface));
    const CraterList secondaries = Secondaries(grid.Bounds(), resolved);
    ApplyCraters(heights, secondaries, *models_.profile, *models_.degradation, apply_, resolved,
                 context.threads);
    for (std::size_t n = 0; n < secondaries.Size(); ++n) {
        const Crater crater = secondaries.At(n);
        features.craterAttributes.push_back(
            ComputeCraterAttributes(crater, models_.degradation->State(crater), *models_.profile));
    }
    features.craters.Append(secondaries);
}

float SecondaryCraterLayer::ApplyAt(float height, double x, double y,
                                    const LayerContext& context) const {
    const double spacing = context.spacing;
    if (!(std::isfinite(spacing) && spacing > 0.0)) {
        throw std::invalid_argument("Sample spacing must be positive and finite");
    }
    if (std::isnan(height)) {
        return height;
    }
    constexpr double kInf = std::numeric_limits<double>::infinity();
    const CraterList secondaries =
        Secondaries({x, y, std::nextafter(x, kInf), std::nextafter(y, kInf)}, spacing);
    for (const std::size_t index : CraterApplyOrder(secondaries, apply_.order)) {
        const Crater crater = secondaries.At(index);
        const double resolution = ResolutionWeight(crater.diameter, spacing, apply_.fade);
        if (resolution == 0.0) {
            continue;
        }
        height = ApplyCraterTo(height, x, y, crater, models_.degradation->State(crater), resolution,
                               std::nullopt, *models_.profile, apply_,
                               OutlineOf(crater, apply_.irregularity));
    }
    return height;
}

}  // namespace terraingen
