#include "terraingen/layers/craters/shaping/apply.h"

#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/log.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// Nodes and weights of 9-point Gauss-Hermite quadrature (the node at 0 and the positive half):
/// the mean of f over a normal distribution of mean m and deviation s is
/// sum(w f(m + sqrt(2) s x)) / sqrt(pi).
constexpr std::array<double, 5> kHermiteNodes{0.0, 0.723551018752838, 1.46855328921667,
                                              2.26658058453184, 3.19099320178153};
constexpr std::array<double, 5> kHermiteWeights{0.720235215606051, 0.432651559002556,
                                                0.0884745273943766, 0.00494362427553695,
                                                0.0000396069772632645};

/// The profile's height at `distance`, its rim rounded by `rim`: blended by `rim.Weight` toward
/// the profile averaged over a normal spread of distances of deviation `sigmaRadii` radii.
double ProfileHeight(const CraterProfile& profile, double distance, double diameter,
                     const CraterState& state, const RimSmoothing& rim) {
    const double height = profile.Height(distance, diameter, state);
    const double radius = 0.5 * diameter;
    const double weight = rim.Weight(distance / radius);
    if (weight == 0.0) {
        return height;
    }
    const double step = std::numbers::sqrt2 * rim.sigmaRadii * radius;
    double smoothed = kHermiteWeights[0] * height;
    for (std::size_t i = 1; i < kHermiteNodes.size(); ++i) {
        for (const double sign : {-1.0, 1.0}) {
            const double at = std::fabs(distance + sign * step * kHermiteNodes[i]);
            smoothed += kHermiteWeights[i] * profile.Height(at, diameter, state);
        }
    }
    smoothed /= std::sqrt(std::numbers::pi);
    return height + weight * (smoothed - height);
}

/// Height of the raster at world point (x, y) by bilinear interpolation of the cell centres, or
/// nothing if a contributing cell is outside the raster or holds no data.
std::optional<double> Sample(const Raster<float>& raster, double x, double y) {
    const Vec2d pixel = raster.Transform().WorldToPixel(x, y);
    const double u = pixel.x - 0.5;
    const double v = pixel.y - 0.5;
    const double col0 = std::floor(u);
    const double row0 = std::floor(v);
    const double maxCol = static_cast<double>(raster.Width()) - 1.0;
    const double maxRow = static_cast<double>(raster.Height()) - 1.0;
    if (col0 < 0.0 || row0 < 0.0 || col0 > maxCol || row0 > maxRow) {
        return std::nullopt;
    }
    const auto c0 = static_cast<std::size_t>(col0);
    const auto r0 = static_cast<std::size_t>(row0);
    const std::size_t c1 = std::min(c0 + 1, raster.Width() - 1);
    const std::size_t r1 = std::min(r0 + 1, raster.Height() - 1);
    const double fu = u - col0;
    const double fv = v - row0;
    const std::array<float, 4> cells{raster(c0, r0), raster(c1, r0), raster(c0, r1),
                                     raster(c1, r1)};
    std::array<double, 4> values{};
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (raster.IsNoData(cells[i])) {
            return std::nullopt;
        }
        values[i] = cells[i];
    }
    return (1.0 - fv) * ((1.0 - fu) * values[0] + fu * values[1]) +
           fv * ((1.0 - fu) * values[2] + fu * values[3]);
}

/// The box a crater's profile can reach, in surface coordinates.
Bounds2d Footprint(const PreparedCrater& prepared, const CraterProfile& profile,
                   const Surface& surface) {
    const Crater& crater = prepared.crater;
    const double reach =
        prepared.outline.MaxScale() * profile.ReachRadii() * (0.5 * crater.diameter);
    return surface.Expand({crater.x, crater.y, crater.x, crater.y}, reach);
}

/// Fits the datums of craters `fitted` of `prepared` to `ground`, or to the raster as given when
/// it is empty.
void FitDatums(std::vector<PreparedCrater>& prepared, const std::vector<std::size_t>& fitted,
               const Raster<float>& raster, const CraterProfile& profile,
               const CraterApplyOptions& options, const CraterSampling& sampling) {
    std::optional<Raster<float>> input;
    HeightSampler ground = sampling.ground;
    if (!ground) {
        input.emplace(raster);
        ground = [&input](double x, double y) {
            return static_cast<float>(
                Sample(*input, x, y).value_or(std::numeric_limits<double>::quiet_NaN()));
        };
    }
    const CraterReliefIndex index(prepared, profile, MaxOutlineScale(options.irregularity),
                                  options.surface);
    ParallelFor(fitted.size(), sampling.threads, [&](std::size_t n) {
        const std::size_t k = fitted[n];
        prepared[k].datum =
            FitCraterDatum(prepared[k].crater, options, DatumGround(ground, index, k));
    });
}

/// A prepared crater with the box its profile can reach.
struct ReachingCrater {
    PreparedCrater* prepared;
    Bounds2d footprint;
    double reach;

    bool Reaches(const Vec2d& p, const Surface& surface) const {
        return surface.Contains(footprint, p) &&
               CraterProfileDistance(prepared->crater, prepared->outline, p.x, p.y, surface) <
                   reach;
    }
};

/// The craters of `prepared` whose profile reaches any of `samples`, in order.
std::vector<ReachingCrater> CratersReaching(const SampleBlock& samples,
                                            std::vector<PreparedCrater>& prepared,
                                            const CraterProfile& profile, const Surface& surface) {
    std::vector<ReachingCrater> reaching;
    for (PreparedCrater& crater : prepared) {
        const ReachingCrater candidate{&crater, Footprint(crater, profile, surface),
                                       profile.ReachRadii() * 0.5 * crater.crater.diameter};
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            if (candidate.Reaches(samples.Position(i), surface)) {
                reaching.push_back(candidate);
                break;
            }
        }
    }
    return reaching;
}

}  // namespace

float ApplyCraterTo(float height, double x, double y, const PreparedCrater& crater,
                    const CraterProfile& profile, const CraterApplyOptions& options) {
    const double diameter = crater.crater.diameter;
    const double radius = 0.5 * diameter;
    const Vec2d offset = options.surface.Offset({crater.crater.x, crater.crater.y}, {x, y});
    const double distance =
        CraterProfileDistance(crater.crater, crater.outline, x, y, options.surface);
    if (distance >= profile.ReachRadii() * radius) {
        return height;
    }
    const double current = height;
    double replaced = 0.0;
    if (crater.datum) {
        const double weight = options.interiorReplacement * options.blend.Weight(distance / radius);
        replaced = weight * (crater.datum->At(offset.x, offset.y) - current);
    }
    const double shaped =
        ProfileHeight(profile, distance, diameter, crater.state, options.rimSmoothing);
    return static_cast<float>(current + crater.resolution * shaped + crater.resolution * replaced);
}

void ApplyCraters(Raster<float>& raster, const CraterList& craters, const CraterShapeModels& models,
                  const CraterApplyOptions& options, const CraterSampling& sampling) {
    ValidateCraterApplyOptions(options);
    RequireNonNegative(sampling.spacing, "Sample spacing");
    if (raster.Empty() || craters.Empty()) {
        return;
    }
    const Stopwatch stopwatch;
    const double resolved = ResolvedSpacing(raster, sampling.spacing, options.surface);
    const CraterProfile& profile = *models.profile;
    std::vector<PreparedCrater> prepared =
        PrepareCraters(craters, options, resolved, *models.degradation);
    std::vector<std::size_t> onRaster;
    std::vector<std::vector<PixelWindow>> windows;
    for (std::size_t k = 0; k < prepared.size(); ++k) {
        std::vector<PixelWindow> around = WindowsAround(
            raster, Footprint(prepared[k], profile, options.surface), options.surface);
        if (!around.empty()) {
            onRaster.push_back(k);
            windows.push_back(std::move(around));
        }
    }
    if (options.datum != CraterDatum::None && !onRaster.empty()) {
        FitDatums(prepared, onRaster, raster, profile, options, sampling);
    }
    ForEachFeatureCell(
        raster, sampling.threads, onRaster.size(),
        [&](std::size_t n) -> const std::vector<PixelWindow>& { return windows[n]; },
        [&](float& cell, const FeatureCell& at) {
            cell = ApplyCraterTo(cell, at.centre.x, at.centre.y, prepared[onRaster[at.feature]],
                                 profile, options);
        });
    LogInfo("craters", "Applied ", onRaster.size(), " of ", craters.Size(), " craters (",
            craters.Size() - prepared.size(), " unresolved at ", resolved, " m, ",
            prepared.size() - onRaster.size(), " off the raster) in ",
            FormatDuration(stopwatch.Milliseconds()));
}

void ApplyCraters(SampleBlock& samples, const CraterList& craters, const CraterShapeModels& models,
                  const CraterApplyOptions& options, const CraterSampling& sampling) {
    if (Raster<float>* grid = samples.Grid()) {
        ApplyCraters(*grid, craters, models, options, sampling);
        return;
    }
    ValidateCraterApplyOptions(options);
    RequirePositive(sampling.spacing, "Sample spacing");
    if (options.datum != CraterDatum::None && !sampling.ground) {
        throw std::invalid_argument("Craters with a datum at points need the ground");
    }
    if (samples.Size() == 0 || craters.Empty()) {
        return;
    }
    const CraterProfile& profile = *models.profile;
    std::vector<PreparedCrater> prepared =
        PrepareCraters(craters, options, sampling.spacing, *models.degradation);
    const std::vector<ReachingCrater> reaching =
        CratersReaching(samples, prepared, profile, options.surface);
    if (options.datum != CraterDatum::None && !reaching.empty()) {
        const CraterReliefIndex index(prepared, profile, MaxOutlineScale(options.irregularity),
                                      options.surface);
        for (const ReachingCrater& crater : reaching) {
            const auto k = static_cast<std::size_t>(crater.prepared - prepared.data());
            crater.prepared->datum = FitCraterDatum(crater.prepared->crater, options,
                                                    DatumGround(sampling.ground, index, k));
        }
    }
    for (std::size_t i = 0; i < samples.Size(); ++i) {
        const Vec2d p = samples.Position(i);
        for (const ReachingCrater& crater : reaching) {
            if (!samples.IsNoData(i) && crater.Reaches(p, options.surface)) {
                samples.Height(i) =
                    ApplyCraterTo(samples.Height(i), p.x, p.y, *crater.prepared, profile, options);
            }
        }
    }
}

}  // namespace terraingen
