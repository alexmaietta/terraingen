// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/albedo/splotches.h"

#include "albedo/bands.h"
#include "terraingen/distribution/power_law.h"
#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/layers/craters/generation/population.h"
#include "terraingen/layers/ground.h"
#include "terraingen/random/samplers.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/span.h"
#include "util/checks.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace terraingen {

namespace {

constexpr double kBinCells = 16.0;

/// Checks that two shares are in [0, 1] and sum to at most 1.
void CheckShares(double dark, double bright, const char* what) {
    if (!(IsInUnitInterval(dark) && IsInUnitInterval(bright) && dark + bright <= 1.0)) {
        throw std::invalid_argument(
            std::string(what) + " dark and bright shares must be in [0, 1] and sum to at most 1");
    }
}

/// Checks the swarms of splotch options.
void CheckSwarms(const SplotchSwarms& s) {
    if (!(IsPositive(s.craterRate) && IsPositive(s.craterSlope) &&
          IsPositive(s.referenceDiameter) && IsPositive(s.referenceCount) &&
          IsPositive(s.minCraterDiameter) && s.maxCraterDiameter > s.minCraterDiameter &&
          std::isfinite(s.maxCraterDiameter) && IsPositive(s.minRange) && s.maxRange > s.minRange &&
          std::isfinite(s.maxRange) && s.mu > 0.0 && s.mu < 1.0)) {
        throw std::invalid_argument("Splotch swarms need positive rates, counts and increasing "
                                    "crater diameters and ranges, with mu in (0, 1)");
    }
    ValidateValueDistribution(s.splotchDiameter);
    if (!(s.splotchDiameter.Lowest() > 0.0 && std::isfinite(s.splotchDiameter.Highest()))) {
        throw std::invalid_argument("Swarm splotch diameters must be positive and bounded");
    }
    ValidateRelativeChange(s.darkChange, "A dark swarm splotch's change");
    ValidateRelativeChange(s.brightChange, "A bright swarm splotch's change");
    for (const Crater& crater : s.craters) {
        if (!(std::isfinite(crater.x) && std::isfinite(crater.y) && IsPositive(crater.diameter) &&
              IsNonNegative(crater.ageGyr))) {
            throw std::invalid_argument(
                "A swarm crater needs a finite position, a positive diameter and an age");
        }
    }
    CheckShares(s.darkShare, s.brightShare, "Swarm");
    RequireUnitInterval(s.wedgeShare, "The swarm wedge share");
    if (!(s.wedgeElongation >= 1.0 && std::isfinite(s.wedgeElongation))) {
        throw std::invalid_argument("A swarm wedge elongation must be finite and at least 1");
    }
}

/// How much of `splotch` covers the point `offset` meters from its center, from 1 inside to 0
/// beyond its lobed, soft edge.
double Cover(const Splotch& splotch, const Vec2d& offset, const SplotchAlbedoOptions& options) {
    const double radius = 0.5 * splotch.mark.diameter;
    double distance = std::hypot(offset.x, offset.y) / radius;
    double angle = 0.0;
    if (splotch.elongation != 1.0 || splotch.wedge) {
        const double stretch = std::sqrt(splotch.elongation);
        const double c = std::cos(splotch.heading);
        const double s = std::sin(splotch.heading);
        const double along = (offset.x * c + offset.y * s) / (radius * stretch);
        double across = (offset.y * c - offset.x * s) * stretch / radius;
        if (splotch.wedge) {
            across /= std::fmax(0.15, 0.5 * (1.0 - along));
        }
        distance = std::hypot(along, across);
        angle = std::atan2(across, along);
    } else if (options.raggedness > 0.0) {
        angle = std::atan2(offset.y, offset.x);
    }
    if (distance >= 1.0) {
        return 0.0;
    }
    const double lobe = EdgeLobe(options.raggedness, kLobeRing, angle, 0.0, splotch.key, 3);
    return SoftEdge(distance / lobe, options.softness);
}

/// The change of `splotch` at `offset` meters from its center, crossing from one side to the
/// other of a mixed splotch.
double SideChange(const Splotch& splotch, const Vec2d& offset) {
    if (splotch.otherChange == splotch.change) {
        return splotch.change;
    }
    const double across =
        (offset.x * std::cos(splotch.split) + offset.y * std::sin(splotch.split)) /
        (0.5 * splotch.mark.diameter);
    const double w = Smoothstep(0.5 + across / 0.6);
    return splotch.change + w * (splotch.otherChange - splotch.change);
}

/// Gives `splotch` its sides from a uniform `kind`: dark below `darkShare`, bright up to
/// `darkShare` + `brightShare`, and mixed beyond, dark on one side and bright on the other.
void SetSides(Splotch& splotch, double kind, double darkShare, double brightShare, double dark,
              double bright) {
    const bool mixed = kind >= darkShare + brightShare;
    splotch.change = kind >= darkShare && !mixed ? bright : dark;
    splotch.otherChange = mixed ? bright : splotch.change;
}

/// The spacing slopes under splotches are read at: `spacing`, or half the smallest splotch when 0.
double SlopeSpacing(double spacing, const SplotchAlbedoOptions& options) {
    return spacing > 0.0 ? spacing : 0.5 * options.minDiameter;
}

/// The slope in degrees from the heights at `GradientPoints` half a splotch `diameter` out; 0
/// where any is missing.
double SlopeDegrees(Span<const float> heights, double diameter) {
    const std::optional<Vec2d> gradient = GradientFromHeights(heights, 0.5 * diameter);
    return gradient ? GradientSlopeDegrees(gradient->x, gradient->y) : 0.0;
}

/// The splotches of a grid's bounds in square bins of `kBinCells` cells, each bin listing those
/// that may reach it.
class SplotchBins {
  public:
    SplotchBins(const RasterGrid& grid, const std::vector<Splotch>& splotches,
                const Surface& surface)
        : bounds_(grid.Bounds()) {
        const double width = bounds_.maxX - bounds_.minX;
        const double height = bounds_.maxY - bounds_.minY;
        bin_ = kBinCells * std::fmax(width / static_cast<double>(grid.width),
                                     height / static_cast<double>(grid.height));
        columns_ = static_cast<std::size_t>(std::ceil(width / bin_));
        rows_ = static_cast<std::size_t>(std::ceil(height / bin_));
        bins_.resize(columns_ * rows_);
        for (const Splotch& splotch : splotches) {
            Add(splotch, surface);
        }
    }

    /// The splotches that may reach (`x`, `y`).
    const std::vector<const Splotch*>& Near(double x, double y) const {
        return bins_[Index(y, bounds_.minY, rows_) * columns_ + Index(x, bounds_.minX, columns_)];
    }

  private:
    std::size_t Index(double value, double origin, std::size_t count) const {
        return ClampIndex(std::floor((value - origin) / bin_), count);
    }

    void Add(const Splotch& splotch, const Surface& surface) {
        const Bounds2d box =
            surface.Expand(PointBounds(splotch.mark.x, splotch.mark.y),
                           0.5 * splotch.mark.diameter * std::sqrt(splotch.elongation));
        if (box.maxX < bounds_.minX || box.minX > bounds_.maxX || box.maxY < bounds_.minY ||
            box.minY > bounds_.maxY) {
            return;
        }
        const std::size_t c0 = Index(box.minX, bounds_.minX, columns_);
        const std::size_t c1 = Index(box.maxX, bounds_.minX, columns_);
        const std::size_t r0 = Index(box.minY, bounds_.minY, rows_);
        const std::size_t r1 = Index(box.maxY, bounds_.minY, rows_);
        for (std::size_t r = r0; r <= r1; ++r) {
            for (std::size_t c = c0; c <= c1; ++c) {
                bins_[r * columns_ + c].push_back(&splotch);
            }
        }
    }

    Bounds2d bounds_;
    double bin_ = 0.0;
    std::size_t columns_ = 0;
    std::size_t rows_ = 0;
    std::vector<std::vector<const Splotch*>> bins_;
};

}  // namespace

double SplotchSwarms::Coefficient(double lifetimeMyr) const {
    return craterRate * std::pow(10.0, craterSlope) * lifetimeMyr;
}

double SplotchSwarms::Count(double diameter) const {
    return referenceCount * std::pow(diameter / referenceDiameter, 3.0 + 1.5 * mu);
}

double SplotchAlbedoOptions::Coefficient() const {
    return rateCoefficient * std::pow(1000.0, rateExponent) * lifetimeMyr;
}

void ValidateSplotchAlbedoOptions(const SplotchAlbedoOptions& options) {
    if (!(IsPositive(options.rateCoefficient) && IsPositive(options.rateExponent) &&
          IsPositive(options.lifetimeMyr) && IsPositive(options.Coefficient()))) {
        throw std::invalid_argument(
            "Splotches need a positive, finite formation rate, slope and lifetime");
    }
    if (!(IsPositive(options.minDiameter) && IsPositive(options.maxDiameter) &&
          options.minDiameter < options.maxDiameter)) {
        throw std::invalid_argument("Splotch diameters must be positive and increasing");
    }
    CheckShares(options.darkShare, options.brightShare, "Splotch");
    ValidateRelativeChange(options.darkChange, "A dark splotch's change");
    ValidateRelativeChange(options.brightChange, "A bright splotch's change");
    if (!(options.softness > 0.0 && options.softness <= 1.0 && options.raggedness >= 0.0 &&
          options.raggedness < 1.0)) {
        throw std::invalid_argument("Splotch softness must be in (0, 1] and raggedness in [0, 1)");
    }
    if (options.steepBrightShare) {
        RequireUnitInterval(*options.steepBrightShare, "The steep bright splotch share");
    }
    if (!(options.steepSlopeDegrees > 0.0 && options.steepSlopeDegrees < 90.0)) {
        throw std::invalid_argument("The steep splotch slope must be in (0, 90) degrees");
    }
    ValidateResolutionFade(options.fade);
    CheckSwarms(options.swarms);
}

SplotchAlbedo::SplotchAlbedo(SplotchAlbedoOptions options, TerrainBase base)
    : options_(std::move(options)), surface_(std::move(base.surface)) {
    ValidateSplotchAlbedoOptions(options_);
    cells_ = MakeCells(surface_);
}

std::vector<Splotch> SplotchAlbedo::Marks(const Bounds2d& region, double spacing) const {
    const SplotchAlbedoOptions& o = options_;
    CraterGenerationOptions generation;
    generation.dMin = o.minDiameter;
    generation.dMax = o.maxDiameter;
    generation.marginRadii = 1.0;
    generation.surface = surface_;
    if (spacing > 0.0) {
        generation.minResolvedDiameter = o.fade.startSamples * spacing;
    }
    const PowerLawSfd density(o.Coefficient(), o.rateExponent);
    const UniformAges ages(1e-3 * o.lifetimeMyr);
    const CraterList marks = GenerateCraters(*cells_, region, density, ages, o.seed, generation);
    std::vector<Splotch> splotches;
    splotches.reserve(marks.Size());
    for (std::size_t n = 0; n < marks.Size(); ++n) {
        if (ResolutionWeight(marks.At(n).diameter, spacing, o.fade) > 0.0) {
            splotches.push_back({marks.At(n)});
        }
    }
    return splotches;
}

std::vector<double> SplotchAlbedo::SteepSlopes(const std::vector<Splotch>& splotches,
                                               double spacing, const Ground* ground) const {
    std::vector<double> slopes(splotches.size(), 0.0);
    if (ground == nullptr || !options_.steepBrightShare) {
        return slopes;
    }
    std::vector<Vec2d> points;
    points.reserve(4 * splotches.size());
    for (const Splotch& splotch : splotches) {
        const auto four =
            GradientPoints(surface_, {splotch.mark.x, splotch.mark.y}, 0.5 * splotch.mark.diameter);
        points.insert(points.end(), four.begin(), four.end());
    }
    std::vector<float> heights(points.size());
    ground->HeightsAt(points, heights, SlopeSpacing(spacing, options_));
    for (std::size_t n = 0; n < splotches.size(); ++n) {
        slopes[n] = SlopeDegrees(Span(heights).subspan(4 * n).first(4), splotches[n].mark.diameter);
    }
    return slopes;
}

void SplotchAlbedo::DrawSides(Splotch& splotch, double slopeDegrees) const {
    const SplotchAlbedoOptions& o = options_;
    double darkShare = o.darkShare;
    double brightShare = o.brightShare;
    if (o.steepBrightShare && slopeDegrees > o.steepSlopeDegrees) {
        const double mixed = 1.0 - darkShare - brightShare;
        brightShare = *o.steepBrightShare;
        darkShare = std::fmax(0.0, 1.0 - mixed - brightShare);
    }
    const IdWords id = SplitId(splotch.mark.id);
    RandomStream random(o.seed, kSplotchStream, id.low, id.high);
    const double kind = random.NextDouble();
    const double d1 = random.NextDouble();
    const double d2 = random.NextDouble();
    const double b1 = random.NextDouble();
    const double b2 = random.NextDouble();
    splotch.split = 2.0 * kPi * random.NextDouble();
    splotch.key = random.NextU32();
    SetSides(splotch, kind, darkShare, brightShare, o.darkChange.Sample(d1, d2),
             o.brightChange.Sample(b1, b2));
}

std::vector<Splotch> SplotchAlbedo::Splotches(const Bounds2d& region, double spacing,
                                              const Ground* ground) const {
    std::vector<Splotch> splotches = Marks(region, spacing);
    const std::vector<double> slopes = SteepSlopes(splotches, spacing, ground);
    for (std::size_t n = 0; n < splotches.size(); ++n) {
        DrawSides(splotches[n], slopes[n]);
    }
    if (options_.swarms.enabled) {
        AddSwarms(region, spacing, splotches);
    }
    std::sort(splotches.begin(), splotches.end(), [](const Splotch& a, const Splotch& b) {
        return a.mark.ageGyr != b.mark.ageGyr ? a.mark.ageGyr < b.mark.ageGyr
                                              : a.mark.id < b.mark.id;
    });
    return splotches;
}

void SplotchAlbedo::AddSwarms(const Bounds2d& region, double spacing,
                              std::vector<Splotch>& splotches) const {
    const SplotchAlbedoOptions& o = options_;
    const SplotchSwarms& s = o.swarms;
    const double margin = 0.5 * s.splotchDiameter.Highest() * std::sqrt(s.wedgeElongation);
    CraterGenerationOptions generation;
    generation.dMin = s.minCraterDiameter;
    generation.dMax = s.maxCraterDiameter;
    generation.surface = surface_;
    const PowerLawSfd density(s.Coefficient(o.lifetimeMyr), s.craterSlope);
    const UniformAges ages(1e-3 * o.lifetimeMyr);
    const CraterList craters =
        GenerateCraters(*cells_, surface_.Expand(region, s.maxRange + margin), density, ages,
                        SubstreamSeed(o.seed, kSwarmCraterStream), generation);
    const Bounds2d wanted = surface_.Expand(region, margin);
    const Bounds2d reach = surface_.Expand(region, s.maxRange + margin);
    const auto addSwarm = [&](const Crater& crater) {
        for (const Splotch& splotch : Swarm(crater, spacing)) {
            if (surface_.Contains(wanted, {splotch.mark.x, splotch.mark.y})) {
                splotches.push_back(splotch);
            }
        }
    };
    for (Crater crater : s.craters) {
        if (crater.id == 0) {
            crater.id = ManualCraterId(crater.x, crater.y, crater.diameter);
        }
        if (surface_.Contains(reach, {crater.x, crater.y})) {
            addSwarm(crater);
        }
    }
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        addSwarm(craters.At(n));
    }
}

Splotch SplotchAlbedo::DrawSwarmSplotch(RandomStream& random, const Crater& crater,
                                        std::uint64_t index) const {
    const SplotchSwarms& s = options_.swarms;
    const double a = 1.5 * s.mu;
    const double near = std::pow(s.minRange, -a);
    const double far = std::pow(s.maxRange, -a);
    const double range = std::pow(near - random.NextDouble() * (near - far), -1.0 / a);
    const double azimuth = 2.0 * kPi * random.NextDouble();
    const double s1 = random.NextDouble();
    const double s2 = random.NextDouble();
    const double kind = random.NextDouble();
    const double d1 = random.NextDouble();
    const double d2 = random.NextDouble();
    const double b1 = random.NextDouble();
    const double b2 = random.NextDouble();
    Splotch splotch;
    splotch.split = 2.0 * kPi * random.NextDouble();
    splotch.key = random.NextU32();
    splotch.wedge = random.NextDouble() < s.wedgeShare;
    const Vec2d at = surface_.Locate({crater.x, crater.y},
                                     {range * std::cos(azimuth), range * std::sin(azimuth)});
    splotch.mark = {at.x, at.y, s.splotchDiameter.Sample(s1, s2), crater.ageGyr,
                    crater.id ^ ((index + 1) * 0x9E3779B97F4A7C15ull)};
    SetSides(splotch, kind, s.darkShare, s.brightShare, s.darkChange.Sample(d1, d2),
             s.brightChange.Sample(b1, b2));
    if (splotch.wedge) {
        const Vec2d back = surface_.Offset(at, {crater.x, crater.y});
        splotch.elongation = s.wedgeElongation;
        splotch.heading = std::atan2(back.y, back.x);
    }
    return splotch;
}

std::vector<Splotch> SplotchAlbedo::Swarm(const Crater& crater, double spacing) const {
    const IdWords id = SplitId(crater.id);
    RandomStream random(options_.seed, kSplotchSwarmStream, id.low, id.high);
    const std::uint64_t count = SamplePoisson(random, options_.swarms.Count(crater.diameter));
    std::vector<Splotch> splotches;
    for (std::uint64_t i = 0; i < count; ++i) {
        const Splotch splotch = DrawSwarmSplotch(random, crater, i);
        if (ResolutionWeight(splotch.mark.diameter, spacing, options_.fade) > 0.0) {
            splotches.push_back(splotch);
        }
    }
    return splotches;
}

double SplotchAlbedo::Change(const Splotch& splotch, double x, double y, double spacing) const {
    return ChangeAt({&splotch}, x, y, spacing);
}

double SplotchAlbedo::ChangeAt(const std::vector<const Splotch*>& splotches, double x, double y,
                               double spacing) const {
    ChangeOverlay overlay;
    for (const Splotch* splotch : splotches) {
        const Vec2d offset = surface_.Offset({splotch->mark.x, splotch->mark.y}, {x, y});
        const double cover = Cover(*splotch, offset, options_);
        if (cover == 0.0) {
            continue;
        }
        const double left = LifeLeft(splotch->mark.ageGyr, options_.lifetimeMyr);
        overlay.Add(std::fmax(0.0, left) * cover,
                    ResolutionWeight(splotch->mark.diameter, spacing, options_.fade) *
                        SideChange(*splotch, offset));
    }
    return overlay.Change();
}

void SplotchAlbedo::ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                            const AlbedoContext& context) const {
    const std::vector<Splotch> splotches =
        Splotches(PointBounds(x, y), context.spacing, context.ground);
    std::vector<const Splotch*> pointers;
    pointers.reserve(splotches.size());
    for (const Splotch& splotch : splotches) {
        pointers.push_back(&splotch);
    }
    ChangeBands(value, bands, ChangeAt(pointers, x, y, context.spacing));
}

void SplotchAlbedo::Apply(std::vector<Raster<float>>& bands, const AlbedoContext& context) const {
    if (bands.empty()) {
        return;
    }
    const RasterGrid grid = RasterGrid::FromRaster(bands.front());
    const std::vector<Splotch> splotches =
        Splotches(grid.Bounds(), context.spacing, context.ground);
    const SplotchBins bins(grid, splotches, surface_);
    const std::size_t count = bands.size();
    const double spacing = context.spacing;
    ForEachAlbedoCell(bands, context.threads, [&](AlbedoSample& value, double x, double y) {
        ChangeBands(value, count, ChangeAt(bins.Near(x, y), x, y, spacing));
    });
}

}  // namespace terraingen
