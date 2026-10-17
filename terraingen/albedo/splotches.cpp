#include "terraingen/albedo/splotches.h"

#include "terraingen/distribution/power_law.h"
#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/layers/craters/generation/population.h"
#include "terraingen/noise/fractal.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace terraingen {

namespace {

constexpr std::uint32_t kSplotchStream = 0x53504c54u;
constexpr std::size_t kBlockRows = 32;
constexpr double kLobeRing = 180.0 / (std::numbers::pi * 45.0);
constexpr double kBinCells = 16.0;

/// Checks that a splotch change is above -1 and bounded.
void CheckChange(const ValueDistribution& change, const char* what) {
    ValidateValueDistribution(change);
    if (!(change.Lowest() > -1.0 && std::isfinite(change.Highest()))) {
        throw std::invalid_argument(std::string(what) + " must be above -1 and bounded");
    }
}

/// How much of `splotch` covers the point `offset` metres from its centre, from 1 inside to 0
/// beyond its lobed, soft edge.
double Cover(const Splotch& splotch, const Vec2d& offset, const SplotchOptions& options) {
    const double radius = 0.5 * splotch.mark.diameter;
    const double distance = std::hypot(offset.x, offset.y);
    if (distance >= radius) {
        return 0.0;
    }
    double lobe = 1.0;
    if (options.raggedness > 0.0) {
        FractalParams params;
        params.seed = splotch.key;
        params.wavelength = 1.0f;
        params.amplitude = 1.0f;
        params.octaves = 3;
        const double angle = std::atan2(offset.y, offset.x);
        const double n = static_cast<double>(
            Fractal3(kLobeRing * std::cos(angle), kLobeRing * std::sin(angle), 0.0, params));
        lobe = 1.0 - options.raggedness * std::clamp(0.5 - n, 0.0, 1.0);
    }
    const double t = distance / (radius * lobe);
    return 1.0 -
           Smoothstep(std::clamp((t - (1.0 - options.softness)) / options.softness, 0.0, 1.0));
}

/// The change of `splotch` at `offset` metres from its centre, crossing from one side to the
/// other of a mixed splotch.
double SideChange(const Splotch& splotch, const Vec2d& offset) {
    if (splotch.otherChange == splotch.change) {
        return splotch.change;
    }
    const double across =
        (offset.x * std::cos(splotch.split) + offset.y * std::sin(splotch.split)) /
        (0.5 * splotch.mark.diameter);
    const double w = Smoothstep(std::clamp(0.5 + across / 0.6, 0.0, 1.0));
    return splotch.change + w * (splotch.otherChange - splotch.change);
}

/// The four points half a splotch `diameter` metres across east, west, north and south of
/// (`x`, `y`).
std::array<Vec2d, 4> SlopePoints(const Surface& surface, double x, double y, double diameter) {
    const double h = 0.5 * diameter;
    return {surface.Locate({x, y}, {h, 0.0}), surface.Locate({x, y}, {-h, 0.0}),
            surface.Locate({x, y}, {0.0, h}), surface.Locate({x, y}, {0.0, -h})};
}

/// The spacing slopes under splotches are read at: `spacing`, or half the smallest splotch when 0.
double SlopeSpacing(double spacing, const SplotchOptions& options) {
    return spacing > 0.0 ? spacing : 0.5 * options.minDiameter;
}

/// The slope in degrees from the heights at `SlopePoints`; 0 where any is NaN.
double SlopeOf(std::span<const float> heights, double diameter) {
    const double dx = static_cast<double>(heights[0] - heights[1]) / diameter;
    const double dy = static_cast<double>(heights[2] - heights[3]) / diameter;
    const double slope = std::atan(std::hypot(dx, dy)) * 180.0 / std::numbers::pi;
    return std::isnan(slope) ? 0.0 : slope;
}

/// Changes every band of `value` by the relative reflectance `change`, never below 0.
void ChangeBands(AlbedoSample& value, std::size_t bands, double change) {
    if (change == 0.0) {
        return;
    }
    const double factor = std::fmax(0.0, 1.0 + change);
    for (std::size_t band = 0; band < bands; ++band) {
        if (!std::isnan(value[band])) {
            value[band] = static_cast<float>(factor * static_cast<double>(value[band]));
        }
    }
}

}  // namespace

double SplotchOptions::Coefficient() const {
    return rateCoefficient * std::pow(1000.0, rateExponent) * lifetimeMyr;
}

void ValidateSplotches(const SplotchOptions& options) {
    if (!(IsPositive(options.rateCoefficient) && IsPositive(options.rateExponent) &&
          IsPositive(options.lifetimeMyr) && IsPositive(options.Coefficient()))) {
        throw std::invalid_argument(
            "Splotches need a positive, finite formation rate, slope and lifetime");
    }
    if (!(IsPositive(options.minDiameter) && IsPositive(options.maxDiameter) &&
          options.minDiameter < options.maxDiameter)) {
        throw std::invalid_argument("Splotch diameters must be positive and increasing");
    }
    if (!(IsInUnitInterval(options.darkShare) && IsInUnitInterval(options.brightShare) &&
          options.darkShare + options.brightShare <= 1.0)) {
        throw std::invalid_argument(
            "Splotch dark and bright shares must be in [0, 1] and sum to at most 1");
    }
    CheckChange(options.darkChange, "A dark splotch's change");
    CheckChange(options.brightChange, "A bright splotch's change");
    if (!(options.softness > 0.0 && options.softness <= 1.0 && options.raggedness >= 0.0 &&
          options.raggedness < 1.0)) {
        throw std::invalid_argument("Splotch softness must be in (0, 1] and raggedness in [0, 1)");
    }
    if (options.steepBrightShare && !IsInUnitInterval(*options.steepBrightShare)) {
        throw std::invalid_argument("The steep bright splotch share must be in [0, 1]");
    }
    if (!(options.steepSlopeDegrees > 0.0 && options.steepSlopeDegrees < 90.0)) {
        throw std::invalid_argument("The steep splotch slope must be in (0, 90) degrees");
    }
    ValidateResolutionFade(options.fade);
}

SplotchAlbedo::SplotchAlbedo(SplotchOptions options, Surface surface)
    : options_(std::move(options)), surface_(std::move(surface)) {
    ValidateSplotches(options_);
    if (surface_.IsBody()) {
        cells_ = std::make_shared<CubeSphereCells>(surface_);
    } else {
        cells_ = std::make_shared<PlanarCells>();
    }
}

double SplotchAlbedo::SlopeDegrees(const Ground& ground, double x, double y, double diameter,
                                   double spacing) const {
    const std::array<Vec2d, 4> points = SlopePoints(surface_, x, y, diameter);
    std::array<float, 4> heights{};
    ground.HeightsAt(points, heights, SlopeSpacing(spacing, options_));
    return SlopeOf(heights, diameter);
}

std::vector<Splotch> SplotchAlbedo::Splotches(const Bounds2d& region, double spacing,
                                              const Ground* ground) const {
    const SplotchOptions& o = options_;
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
    std::vector<double> slopes(splotches.size(), 0.0);
    if (ground != nullptr && o.steepBrightShare) {
        std::vector<Vec2d> points;
        points.reserve(4 * splotches.size());
        for (const Splotch& splotch : splotches) {
            const auto four =
                SlopePoints(surface_, splotch.mark.x, splotch.mark.y, splotch.mark.diameter);
            points.insert(points.end(), four.begin(), four.end());
        }
        std::vector<float> heights(points.size());
        ground->HeightsAt(points, heights, SlopeSpacing(spacing, o));
        for (std::size_t n = 0; n < splotches.size(); ++n) {
            slopes[n] = SlopeOf(std::span(heights).subspan(4 * n, 4), splotches[n].mark.diameter);
        }
    }
    for (std::size_t n = 0; n < splotches.size(); ++n) {
        Splotch& splotch = splotches[n];
        double darkShare = o.darkShare;
        double brightShare = o.brightShare;
        if (o.steepBrightShare && slopes[n] > o.steepSlopeDegrees) {
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
        splotch.split = 2.0 * std::numbers::pi * random.NextDouble();
        splotch.key = random.NextU32();
        const double dark = o.darkChange.Sample(d1, d2);
        const double bright = o.brightChange.Sample(b1, b2);
        splotch.change = kind < darkShare + brightShare && kind >= darkShare ? bright : dark;
        splotch.otherChange = kind < darkShare + brightShare ? splotch.change : bright;
    }
    std::sort(splotches.begin(), splotches.end(), [](const Splotch& a, const Splotch& b) {
        return a.mark.ageGyr != b.mark.ageGyr ? a.mark.ageGyr < b.mark.ageGyr
                                              : a.mark.id < b.mark.id;
    });
    return splotches;
}

double SplotchAlbedo::Change(const Splotch& splotch, double x, double y, double spacing) const {
    return ChangeAt({&splotch}, x, y, spacing);
}

double SplotchAlbedo::ChangeAt(const std::vector<const Splotch*>& splotches, double x, double y,
                               double spacing) const {
    double change = 0.0;
    double open = 1.0;
    for (const Splotch* splotch : splotches) {
        const Vec2d offset = surface_.Offset({splotch->mark.x, splotch->mark.y}, {x, y});
        const double cover = Cover(*splotch, offset, options_);
        if (cover == 0.0) {
            continue;
        }
        const double left = 1.0 - 1e3 * splotch->mark.ageGyr / options_.lifetimeMyr;
        const double strength = std::fmax(0.0, left) * cover;
        change += open * strength *
                  ResolutionWeight(splotch->mark.diameter, spacing, options_.fade) *
                  SideChange(*splotch, offset);
        open *= 1.0 - strength;
    }
    return change;
}

void SplotchAlbedo::ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                            const AlbedoContext& context) const {
    ValidateAlbedoContext(context);
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
    ValidateAlbedoContext(context);
    if (bands.empty()) {
        return;
    }
    const RasterGrid grid = RasterGrid::FromRaster(bands.front());
    const Bounds2d bounds = grid.Bounds();
    const std::vector<Splotch> splotches = Splotches(bounds, context.spacing, context.ground);
    const double width = bounds.maxX - bounds.minX;
    const double height = bounds.maxY - bounds.minY;
    const double bin = kBinCells * std::fmax(width / static_cast<double>(grid.width),
                                             height / static_cast<double>(grid.height));
    const auto columns = static_cast<std::size_t>(std::ceil(width / bin));
    const auto rows = static_cast<std::size_t>(std::ceil(height / bin));
    const auto index = [&](double value, double origin, std::size_t count) {
        const double i = std::floor((value - origin) / bin);
        return static_cast<std::size_t>(std::clamp(i, 0.0, static_cast<double>(count - 1)));
    };
    std::vector<std::vector<const Splotch*>> bins(columns * rows);
    for (const Splotch& splotch : splotches) {
        const Bounds2d box = surface_.Expand(PointBounds(splotch.mark.x, splotch.mark.y),
                                             0.5 * splotch.mark.diameter);
        if (box.maxX < bounds.minX || box.minX > bounds.maxX || box.maxY < bounds.minY ||
            box.minY > bounds.maxY) {
            continue;
        }
        const std::size_t c0 = index(box.minX, bounds.minX, columns);
        const std::size_t c1 = index(box.maxX, bounds.minX, columns);
        const std::size_t r0 = index(box.minY, bounds.minY, rows);
        const std::size_t r1 = index(box.maxY, bounds.minY, rows);
        for (std::size_t r = r0; r <= r1; ++r) {
            for (std::size_t c = c0; c <= c1; ++c) {
                bins[r * columns + c].push_back(&splotch);
            }
        }
    }
    const std::size_t count = bands.size();
    const double spacing = context.spacing;
    ForEachAlbedoBlock(bands, context.threads, kBlockRows, [&](std::size_t, std::size_t) {
        return AlbedoCellVisitor([&](AlbedoSample& value, double x, double y) {
            const auto& near =
                bins[index(y, bounds.minY, rows) * columns + index(x, bounds.minX, columns)];
            ChangeBands(value, count, ChangeAt(near, x, y, spacing));
        });
    });
}

}  // namespace terraingen
