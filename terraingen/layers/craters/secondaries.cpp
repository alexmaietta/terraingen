#include "terraingen/layers/craters/secondaries.h"

#include "terraingen/layers/fade.h"
#include "terraingen/noise/value.h"
#include "terraingen/random/philox.h"
#include "terraingen/random/samplers.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

constexpr std::uint32_t kSecondaryStream = 0x53454344u;

/// The primary's ID mixed with a cell's indices into a secondary's ID.
std::uint64_t SecondaryId(std::uint64_t primary, int band, std::int64_t ring, std::int64_t sector,
                          std::uint32_t index) {
    const IdWords id = SplitId(primary);
    const U32x4 block = Philox4x32(
        {id.low, id.high,
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

Footprint SecondaryFootprint(const Vec2d& centre, const Bounds2d& region, const Surface& surface) {
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

/// The sectors [first, last) of a ring of `sectors` sectors of angle `width` at inner radius `r0`
/// that can reach a footprint, in cells of side `cell`; indices may run past a turn.
std::pair<std::int64_t, std::int64_t> SectorRange(const Footprint& footprint, double cell,
                                                  double r0, double width, std::int64_t sectors) {
    if (footprint.half < std::numbers::pi) {
        const double margin = cell / r0 + width;
        const double from = footprint.mid - footprint.half - margin;
        const double to = footprint.mid + footprint.half + margin;
        if (to - from < 2.0 * std::numbers::pi) {
            return {static_cast<std::int64_t>(std::floor(from / width)),
                    static_cast<std::int64_t>(std::ceil(to / width))};
        }
    }
    return {0, sectors};
}

/// One primary, with what its secondaries depend on.
struct Primary {
    Crater crater;
    double onset = 0.0;      ///< `SecondaryOnsetRange`.
    double outer = 0.0;      ///< The outer range.
    double density0 = 0.0;   ///< Secondaries per m^2 at the onset at least `reference` across.
    double reference = 0.0;  ///< `countFraction` times the diameter.
    Footprint footprint;     ///< The region secondaries are wanted in, seen from the primary.
    /// Where its secondaries gather; none, without rays, spreads them evenly.
    std::optional<SecondaryRayField> rays;
};

/// One ring of a size band of a primary's polar cells.
struct Ring {
    int band = 0;
    double low = 0.0;        ///< Smallest diameter of the band.
    double bandShare = 0.0;  ///< The band's share of the cumulative count at `reference`.
    std::int64_t index = 0;
    double r0 = 0.0;
    double r1 = 0.0;
    double r = 0.0;      ///< Chapman's distance, 1 + (r0 - onset) / D.
    double width = 0.0;  ///< Angle of a sector.
    double mean = 0.0;   ///< Secondaries expected in a sector.
    double peak = 1.0;   ///< `SecondaryRayField::Peak` over the ring; 1 without rays.
};

}  // namespace

double SecondaryStretch(const SecondaryShapes& shapes) {
    return shapes.elongatedShare > 0.0 ? std::sqrt(shapes.ellipticity.Highest()) : 1.0;
}

namespace {

/// Draws the secondaries of primaries into one list, keeping count of those expected.
class Scatter {
  public:
    Scatter(const SecondaryCraterLayer& layer, const Surface& surface, const Bounds2d& region,
            const Bounds2d& wanted, double reachRadii, double skipBelow)
        : layer_(layer), options_(layer.Options()), surface_(surface), region_(region),
          wanted_(wanted), reachRadii_(reachRadii), skipBelow_(skipBelow),
          maxStretch_(SecondaryStretch(layer.Options().shapes)) {}

    /// Draws the secondaries `crater` throws, if it is a selected primary.
    void DrawSecondaries(const Crater& crater) {
        if (!options_.selection.Includes(crater)) {
            return;
        }
        const double diameter = crater.diameter;
        Primary primary;
        primary.crater = crater;
        primary.onset = SecondaryOnsetRange(diameter);
        primary.outer = layer_.OuterRange(diameter);
        if (!(primary.outer > primary.onset)) {
            return;
        }
        primary.footprint = SecondaryFootprint({crater.x, crater.y}, wanted_, surface_);
        primary.density0 = *options_.onsetCount / (0.25 * std::numbers::pi * diameter * diameter);
        primary.reference = options_.countFraction * diameter;
        primary.rays = SecondaryRayField::Make(crater, options_.rays);
        const double biggest = std::fmin(
            options_.maxDiameter, std::fmax(SingerMaxSecondaryDiameter(diameter, primary.onset),
                                            SingerMaxSecondaryDiameter(diameter, primary.outer)));
        const int firstBand = std::ilogb(std::fmax(options_.minDiameter, 1e-9));
        const int lastBand = std::ilogb(biggest);
        for (int band = firstBand; band <= lastBand; ++band) {
            DrawBand(primary, band);
        }
    }

    CraterList Take() {
        return std::move(secondaries_);
    }

    /// The outlines of the elongated secondaries drawn, by ID.
    std::unordered_map<std::uint64_t, SecondaryOutline> TakeOutlines() {
        return std::move(outlines_);
    }

  private:
    void DrawBand(const Primary& primary, int band) {
        const double low = std::fmax(options_.minDiameter, std::ldexp(1.0, band));
        const double high = std::fmin(options_.maxDiameter, std::ldexp(1.0, band + 1));
        if (!(high > low) || high <= skipBelow_) {
            return;
        }
        const double diameter = primary.crater.diameter;
        const double far = std::fmin(primary.outer, SingerMaxRange(diameter, low));
        const double cell = options_.cellSizeFactor * high;
        const double inner = std::fmax(primary.onset, primary.footprint.near - cell);
        const double outerEdge = std::fmin(far, primary.footprint.far + cell);
        if (!(outerEdge > inner)) {
            return;
        }
        const auto firstRing =
            static_cast<std::int64_t>(std::floor((inner - primary.onset) / cell));
        const auto lastRing =
            static_cast<std::int64_t>(std::ceil((outerEdge - primary.onset) / cell));
        const double s = options_.sizeSlope;
        Ring ring;
        ring.band = band;
        ring.low = low;
        ring.bandShare =
            std::pow(low / primary.reference, -s) - std::pow(high / primary.reference, -s);
        for (ring.index = firstRing; ring.index < lastRing; ++ring.index) {
            ring.r0 = primary.onset + static_cast<double>(ring.index) * cell;
            ring.r1 = std::fmin(far, ring.r0 + cell);
            if (!(ring.r1 > ring.r0)) {
                continue;
            }
            DrawRing(primary, ring, cell);
        }
    }

    void DrawRing(const Primary& primary, Ring& ring, double cell) {
        const auto sectors = std::max<std::int64_t>(
            1, static_cast<std::int64_t>(
                   std::ceil(2.0 * std::numbers::pi * 0.5 * (ring.r0 + ring.r1) / cell)));
        ring.width = 2.0 * std::numbers::pi / static_cast<double>(sectors);
        const auto [first, last] =
            SectorRange(primary.footprint, cell, ring.r0, ring.width, sectors);
        ring.r = 1.0 + (ring.r0 - primary.onset) / primary.crater.diameter;
        const double area = 0.5 * ring.width * (ring.r1 * ring.r1 - ring.r0 * ring.r0);
        ring.mean = primary.density0 / (ring.r * ring.r) * ring.bandShare * area;
        if (primary.rays) {
            ring.peak = primary.rays->Peak(ring.r0, ring.r1);
            ring.mean *= ring.peak;
        }
        for (std::int64_t raw = first; raw < last; ++raw) {
            expected_ += ring.mean;
            if (expected_ > options_.maxExpected) {
                throw std::invalid_argument("Expected secondary count exceeds maxExpected");
            }
            DrawSector(primary, ring, ((raw % sectors) + sectors) % sectors);
        }
    }

    void DrawSector(const Primary& primary, const Ring& ring, std::int64_t sector) {
        const Crater& crater = primary.crater;
        const IdWords id = SplitId(crater.id);
        RandomStream random(options_.seed, kSecondaryStream, id.low ^ id.high,
                            static_cast<std::uint32_t>(ring.band) ^
                                (static_cast<std::uint32_t>(ring.index) << 8),
                            static_cast<std::uint32_t>(sector));
        const double s = options_.sizeSlope;
        const double reference = primary.reference;
        const std::uint64_t count = SamplePoisson(random, ring.mean);
        for (std::uint64_t index = 0; index < count; ++index) {
            const double range = std::sqrt(
                ring.r0 * ring.r0 + random.NextDouble() * (ring.r1 * ring.r1 - ring.r0 * ring.r0));
            const double azimuth = (static_cast<double>(sector) + random.NextDouble()) * ring.width;
            const double u = random.NextDouble();
            const double size =
                reference *
                std::pow(std::pow(ring.low / reference, -s) - u * ring.bandShare, -1.0 / s);
            const double keep = random.NextDouble();
            const double rayKeep = primary.rays ? random.NextDouble() : 0.0;
            const SecondaryShapes& shapes = options_.shapes;
            const double e0 = shapes.elongatedShare > 0.0 ? random.NextDouble() : 1.0;
            const double e1 = shapes.elongatedShare > 0.0 ? random.NextDouble() : 0.0;
            const double e2 = shapes.elongatedShare > 0.0 ? random.NextDouble() : 0.0;
            const double here = 1.0 + (range - primary.onset) / crater.diameter;
            if (keep >= (ring.r * ring.r) / (here * here) ||
                size >= SingerMaxSecondaryDiameter(crater.diameter, range)) {
                continue;
            }
            if (primary.rays && rayKeep * ring.peak >= primary.rays->Density(range, azimuth)) {
                continue;
            }
            const Vec2d at = surface_.Locate(
                {crater.x, crater.y}, {range * std::cos(azimuth), range * std::sin(azimuth)});
            const double margin = 0.5 * reachRadii_ * maxStretch_ * size;
            if (!surface_.Contains(surface_.Expand(region_, margin), at)) {
                continue;
            }
            const std::uint64_t secondary = SecondaryId(crater.id, ring.band, ring.index, sector,
                                                        static_cast<std::uint32_t>(index));
            secondaries_.Append({at.x, at.y, size, crater.ageGyr, secondary});
            if (e0 < shapes.elongatedShare) {
                const double toPrimary =
                    std::clamp((primary.outer - range) / (primary.outer - primary.onset), 0.0, 1.0);
                const Vec2d back = surface_.Offset(at, {crater.x, crater.y});
                outlines_[secondary] = {1.0 + (shapes.ellipticity.Sample(e1, e2) - 1.0) * toPrimary,
                                        std::atan2(back.y, back.x)};
            }
        }
    }

    const SecondaryCraterLayer& layer_;
    const SecondaryCraterOptions& options_;
    const Surface& surface_;
    const Bounds2d& region_;
    const Bounds2d& wanted_;
    double reachRadii_;
    double skipBelow_;
    CraterList secondaries_;
    std::unordered_map<std::uint64_t, SecondaryOutline> outlines_;
    double expected_ = 0.0;
    double maxStretch_ = 1.0;
};

}  // namespace

SecondaryCraterLayer::SecondaryCraterLayer(std::shared_ptr<const CraterLayer> primaries,
                                           CraterShapeModels models, SecondaryCraterOptions options,
                                           CraterApplyOptions apply)
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
    return SecondaryOuterRange(diameter, options_);
}

CraterAttributes SecondaryCraterLayer::Attributes(const Crater& crater) const {
    return ComputeCraterAttributes(crater, models_.degradation->State(crater), *models_.profile);
}

CraterFeatures SecondaryCraterLayer::CratersOver(const RasterGrid& grid, double spacing) const {
    CraterFeatures placed{
        Secondaries(grid.Bounds(), std::max(spacing, grid.Spacing(apply_.surface))), {}};
    for (std::size_t n = 0; n < placed.craters.Size(); ++n) {
        placed.attributes.push_back(Attributes(placed.craters.At(n)));
    }
    return placed;
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
    std::unordered_map<std::uint64_t, SecondaryOutline> outlines;
    return Secondaries(region, spacing, outlines);
}

CraterList SecondaryCraterLayer::Secondaries(
    const Bounds2d& region, double spacing,
    std::unordered_map<std::uint64_t, SecondaryOutline>& outlines) const {
    const Surface& surface = apply_.surface;
    const double reachRadii = CraterReachRadii(*models_.profile, apply_);
    const double largest = primaries_->MaxFeatureDiameter();
    const Bounds2d wanted = surface.Expand(
        region, 0.5 * reachRadii * SecondaryStretch(options_.shapes) * MaxFeatureDiameter());
    const CraterList primaries =
        primaries_->Craters(surface.Expand(wanted, OuterRange(largest)), 0.0);
    const double skipBelow = spacing > 0.0 ? apply_.fade.startSamples * spacing : 0.0;
    Scatter scatter(*this, surface, region, wanted, reachRadii, skipBelow);
    for (std::size_t n = 0; n < primaries.Size(); ++n) {
        scatter.DrawSecondaries(primaries.At(n));
    }
    outlines = scatter.TakeOutlines();
    return scatter.Take();
}

void SecondaryCraterLayer::ApplySamples(SampleBlock& samples, const LayerContext& context) const {
    const Raster<float>* grid = samples.Grid();
    const double spacing =
        grid != nullptr
            ? std::max(context.spacing, RasterGrid::FromRaster(*grid).Spacing(apply_.surface))
            : context.spacing;
    std::unordered_map<std::uint64_t, SecondaryOutline> outlines;
    const CraterList secondaries = Secondaries(samples.Bounds(), spacing, outlines);
    CraterSampling sampling{spacing, context.threads};
    if (!outlines.empty()) {
        sampling.reshape = [&outlines](const Crater& crater, CraterOutline& outline) {
            const auto found = outlines.find(crater.id);
            if (found != outlines.end()) {
                outline.ellipticity = found->second.ellipticity;
                outline.heading = found->second.heading;
            }
        };
    }
    ApplyCraters(samples, secondaries, models_, apply_, sampling);
}

}  // namespace terraingen
