// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/secondaries.h"

#include "terraingen/layers/fade.h"
#include "terraingen/layers/source_resolved.h"
#include "terraingen/noise/value.h"
#include "terraingen/random/philox.h"
#include "terraingen/random/samplers.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// The primary's ID mixed with a cell's indices into a secondary's ID.
std::uint64_t SecondaryId(std::uint64_t primary, int band, std::int64_t ring, std::int64_t sector,
                          std::uint32_t index) {
    const IdWords id = SplitId(primary);
    const U32x4 block = Philox4x32(
        {id.low, id.high,
         static_cast<std::uint32_t>(band) ^ (static_cast<std::uint32_t>(ring) << 8), index},
        {static_cast<std::uint32_t>(sector), kSecondaryIdStream});
    return JoinWords(block.x, block.y);
}

/// A region seen from a primary's center: its nearest and farthest distances (m), and its
/// directions as an interval around `mid` of half-width `half` (radians counter-clockwise from
/// east).
struct Footprint {
    double near = 0.0;
    double far = 0.0;
    double mid = 0.0;
    double half = kPi;
};

/// `region` seen from `center` on `surface`; the whole circle when `region` holds the center.
Footprint SecondaryFootprint(const Vec2d& center, const Bounds2d& region, const Surface& surface) {
    Footprint footprint;
    std::vector<Vec2d> corners;
    for (const double x : {region.minX, region.maxX}) {
        for (const double y : {region.minY, region.maxY}) {
            corners.push_back(surface.Offset(center, {x, y}));
        }
    }
    constexpr double kInf = std::numeric_limits<double>::infinity();
    double minX = kInf;
    double maxX = -kInf;
    double minY = kInf;
    double maxY = -kInf;
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
        half = std::fmax(
            half, std::fabs(std::remainder(std::atan2(c.y, c.x) - footprint.mid, 2.0 * kPi)));
    }
    footprint.half = std::fmin(kPi, half);
    return footprint;
}

/// The sectors [first, last) of a ring of `sectors` sectors of angle `width` at inner radius `r0`
/// that can reach a footprint, in cells of side `cell`; indices may run past a turn.
std::pair<std::int64_t, std::int64_t> SectorRange(const Footprint& footprint, double cell,
                                                  double r0, double width, std::int64_t sectors) {
    if (footprint.half < kPi) {
        const double margin = cell / r0 + width;
        const double from = footprint.mid - footprint.half - margin;
        const double to = footprint.mid + footprint.half + margin;
        if (to - from < 2.0 * kPi) {
            return {static_cast<std::int64_t>(std::floor(from / width)),
                    static_cast<std::int64_t>(std::ceil(to / width))};
        }
    }
    return {0, sectors};
}

/// One primary, with the density its secondaries follow and the region they are wanted in.
struct Primary {
    Crater crater;
    SecondaryDensity density;
    Footprint footprint;  ///< The region secondaries are wanted in, seen from the primary.
};

/// One ring of a size band of a primary's polar cells.
struct Ring {
    int band = 0;
    double low = 0.0;        ///< Smallest diameter of the band.
    double bandShare = 0.0;  ///< The band's share of the cumulative count at the reference.
    std::int64_t index = 0;
    double r0 = 0.0;
    double r1 = 0.0;
    double r = 0.0;      ///< Chapman's scaled range at `r0`.
    double width = 0.0;  ///< Angle of a sector.
    double mean = 0.0;   ///< Secondaries expected in a sector.
    double peak = 1.0;   ///< `SecondaryRayField::Peak` over the ring; 1 without rays.
};

/// One secondary drawn in a sector, before it is kept or thinned.
struct Candidate {
    double range = 0.0;
    double azimuth = 0.0;
    double size = 0.0;
    double keep = 0.0;
    double rayKeep = 0.0;
    double e0 = 1.0;  ///< Whether it is elongated, against `elongatedShare`.
    double e1 = 0.0;  ///< The draws of its ellipticity.
    double e2 = 0.0;
};

/// Draws the secondaries of primaries into one list, keeping count of those expected.
class Scatter {
  public:
    Scatter(const SecondaryCraterOptions& options, const SurfaceField* limit,
            const Surface& surface, const Bounds2d& region, const Bounds2d& wanted,
            double reachRadii, double skipBelow)
        : options_(options), limit_(limit), surface_(surface), region_(region), wanted_(wanted),
          reachRadii_(reachRadii), skipBelow_(skipBelow),
          maxStretch_(SecondaryStretch(options.shapes)) {}

    /// Draws the secondaries `crater` throws, if it is a selected primary.
    void DrawSecondaries(const Crater& crater) {
        Primary primary{crater, SecondaryDensity(crater, options_), {}};
        if (!(primary.density.Reach() > 0.0)) {
            return;
        }
        primary.footprint = SecondaryFootprint({crater.x, crater.y}, wanted_, surface_);
        const int firstBand = std::ilogb(std::fmax(options_.minDiameter, 1e-9));
        const int lastBand = std::ilogb(LargestSecondary(crater.diameter, options_));
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
        const SecondaryDensity& density = primary.density;
        const double far = std::fmin(density.Reach(), SingerMaxRange(primary.crater.diameter, low));
        const double cell = options_.cellSizeFactor * high;
        const double inner = std::fmax(density.Onset(), primary.footprint.near - cell);
        const double outerEdge = std::fmin(far, primary.footprint.far + cell);
        if (!(outerEdge > inner)) {
            return;
        }
        const auto firstRing =
            static_cast<std::int64_t>(std::floor((inner - density.Onset()) / cell));
        const auto lastRing =
            static_cast<std::int64_t>(std::ceil((outerEdge - density.Onset()) / cell));
        Ring ring;
        ring.band = band;
        ring.low = low;
        ring.bandShare = density.SizeShare(low, high);
        for (ring.index = firstRing; ring.index < lastRing; ++ring.index) {
            ring.r0 = density.Onset() + static_cast<double>(ring.index) * cell;
            ring.r1 = std::fmin(far, ring.r0 + cell);
            if (!(ring.r1 > ring.r0)) {
                continue;
            }
            DrawRing(primary, ring, cell);
        }
    }

    void DrawRing(const Primary& primary, Ring& ring, double cell) {
        const SecondaryDensity& density = primary.density;
        const auto sectors = std::max<std::int64_t>(
            1, static_cast<std::int64_t>(std::ceil(2.0 * kPi * 0.5 * (ring.r0 + ring.r1) / cell)));
        ring.width = 2.0 * kPi / static_cast<double>(sectors);
        const auto [first, last] =
            SectorRange(primary.footprint, cell, ring.r0, ring.width, sectors);
        ring.r = density.ScaledRange(ring.r0);
        const double area = 0.5 * ring.width * (ring.r1 * ring.r1 - ring.r0 * ring.r0);
        ring.mean = density.OnsetDensity() / (ring.r * ring.r) * ring.bandShare * area;
        if (density.Rays()) {
            ring.peak = density.Rays()->Peak(ring.r0, ring.r1);
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
        const IdWords id = SplitId(primary.crater.id);
        RandomStream random(options_.seed, kSecondaryStream, id.low ^ id.high,
                            static_cast<std::uint32_t>(ring.band) ^
                                (static_cast<std::uint32_t>(ring.index) << 8),
                            static_cast<std::uint32_t>(sector));
        const std::uint64_t count = SamplePoisson(random, ring.mean);
        for (std::uint64_t index = 0; index < count; ++index) {
            const Candidate candidate = DrawCandidate(random, primary, ring, sector);
            if (Keeps(primary, ring, candidate)) {
                Place(primary, ring, sector, static_cast<std::uint32_t>(index), candidate);
            }
        }
    }

    /// Draws one candidate of `sector` from `random`.
    Candidate DrawCandidate(RandomStream& random, const Primary& primary, const Ring& ring,
                            std::int64_t sector) const {
        const double s = options_.sizeSlope;
        const double reference = primary.density.Reference();
        Candidate candidate;
        candidate.range = std::sqrt(ring.r0 * ring.r0 +
                                    random.NextDouble() * (ring.r1 * ring.r1 - ring.r0 * ring.r0));
        candidate.azimuth = (static_cast<double>(sector) + random.NextDouble()) * ring.width;
        const double u = random.NextDouble();
        candidate.size =
            reference * std::pow(std::pow(ring.low / reference, -s) - u * ring.bandShare, -1.0 / s);
        candidate.keep = random.NextDouble();
        candidate.rayKeep = primary.density.Rays() ? random.NextDouble() : 0.0;
        if (options_.shapes.elongatedShare > 0.0) {
            candidate.e0 = random.NextDouble();
            candidate.e1 = random.NextDouble();
            candidate.e2 = random.NextDouble();
        }
        return candidate;
    }

    /// Whether `candidate` survives the thinning by range, the largest size there, and the rays.
    static bool Keeps(const Primary& primary, const Ring& ring, const Candidate& candidate) {
        const SecondaryDensity& density = primary.density;
        const double here = density.ScaledRange(candidate.range);
        if (candidate.keep >= (ring.r * ring.r) / (here * here) ||
            candidate.size >=
                SingerMaxSecondaryDiameter(primary.crater.diameter, candidate.range)) {
            return false;
        }
        return !density.Rays() || !(candidate.rayKeep * ring.peak >=
                                    density.Rays()->Density(candidate.range, candidate.azimuth));
    }

    /// Appends `candidate` when it lands near the region, with its outline when elongated.
    void Place(const Primary& primary, const Ring& ring, std::int64_t sector, std::uint32_t index,
               const Candidate& candidate) {
        const Crater& crater = primary.crater;
        const double range = candidate.range;
        const Vec2d at =
            surface_.Locate({crater.x, crater.y}, {range * std::cos(candidate.azimuth),
                                                   range * std::sin(candidate.azimuth)});
        const double margin = 0.5 * reachRadii_ * maxStretch_ * candidate.size;
        if (!surface_.Contains(surface_.Expand(region_, margin), at) ||
            InSource(limit_, candidate.size, at.x, at.y)) {
            return;
        }
        const std::uint64_t secondary =
            SecondaryId(crater.id, ring.band, ring.index, sector, index);
        secondaries_.Append({at.x, at.y, candidate.size, crater.ageGyr, secondary});
        const SecondaryShapes& shapes = options_.shapes;
        if (candidate.e0 < shapes.elongatedShare) {
            const double onset = primary.density.Onset();
            const double outer = primary.density.Reach();
            const double toPrimary = std::clamp((outer - range) / (outer - onset), 0.0, 1.0);
            const Vec2d back = surface_.Offset(at, {crater.x, crater.y});
            outlines_[secondary] = {
                1.0 + (shapes.ellipticity.Sample(candidate.e1, candidate.e2) - 1.0) * toPrimary,
                std::atan2(back.y, back.x)};
        }
    }

    const SecondaryCraterOptions& options_;
    const SurfaceField* limit_;
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

double SecondaryStretch(const SecondaryShapes& shapes) {
    return shapes.elongatedShare > 0.0 ? std::sqrt(shapes.ellipticity.Highest()) : 1.0;
}

SecondaryCraterLayer::SecondaryCraterLayer(std::shared_ptr<const CraterLayer> primaries,
                                           CraterShapeModels models, SecondaryCraterOptions options,
                                           CraterApplyOptions apply,
                                           std::shared_ptr<const Source> source)
    : primaries_(std::move(primaries)), models_(std::move(models)), options_(options),
      apply_(apply) {
    if (!primaries_ || !models_.degradation) {
        throw std::invalid_argument("Secondary craters need primaries and degradation");
    }
    models_.profile = MatchedProfile(std::move(models_.profile), *models_.degradation);
    ValidateSecondaryCraterOptions(options_);
    ValidateCraterApplyOptions(apply_);
    if (apply_.datum != CraterDatum::None) {
        throw std::invalid_argument("Secondary craters are additive; datums are not supported");
    }
    apply_.surface = primaries_->Options().surface;
    limit_ = LayerResolvedLimit(source, options_.sourceResolvesPixels, apply_.surface);
}

double SecondaryCraterLayer::OuterRange(double diameter) const {
    return SecondaryOuterRange(diameter, options_);
}

CraterFeatures SecondaryCraterLayer::CratersOver(const RasterGrid& grid, double spacing) const {
    return DescribeCraters(Secondaries(grid.Bounds(), ListedSpacing(grid, spacing, apply_.surface)),
                           models_);
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
    Scatter scatter(options_, limit_.get(), surface, region, wanted, reachRadii, skipBelow);
    for (std::size_t n = 0; n < primaries.Size(); ++n) {
        scatter.DrawSecondaries(primaries.At(n));
    }
    outlines = scatter.TakeOutlines();
    return scatter.Take();
}

void SecondaryCraterLayer::ApplySamples(SampleBlock& samples, const LayerContext& context) const {
    const double spacing = samples.ResolvedSpacing(context.spacing, apply_.surface);
    std::unordered_map<std::uint64_t, SecondaryOutline> outlines;
    const CraterList secondaries = Secondaries(samples.Bounds(), spacing, outlines);
    CraterSampling sampling = SamplingFrom(context, spacing, this);
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
