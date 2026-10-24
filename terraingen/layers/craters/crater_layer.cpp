// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/crater_layer.h"

#include "terraingen/distribution/equilibrium.h"
#include "terraingen/distribution/minimum.h"
#include "terraingen/distribution/neukum.h"
#include "terraingen/distribution/power_law.h"
#include "terraingen/distribution/production_surface.h"
#include "terraingen/distribution/spliced.h"
#include "terraingen/distribution/summed.h"
#include "terraingen/layers/craters/ages/steady_state.h"
#include "terraingen/layers/craters/degradation/diffusion.h"
#include "terraingen/layers/craters/degradation/diffusivities.h"
#include "terraingen/layers/craters/generation/spacing.h"
#include "terraingen/layers/craters/profiles/martin_parkes_dunstan.h"
#include "terraingen/layers/craters/shaping/morphometry.h"
#include "terraingen/layers/source_resolved.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/bisection.h"
#include "terraingen/util/cells.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// The largest primary (m) searched for the smallest one throwing secondaries a map measures,
/// about where Singer et al.'s largest secondary stops growing with the primary.
constexpr double kLargestThrower = 5e5;

}  // namespace

CraterLayer::CraterLayer(CraterModels models, CraterLayerOptions options,
                         std::shared_ptr<const Source> source)
    : models_(std::move(models)), options_(std::move(options)) {
    if (!models_.degradation) {
        throw std::invalid_argument("A crater layer needs a degradation model");
    }
    if (options_.generateRandom && (!models_.density || !models_.ages)) {
        throw std::invalid_argument("A crater layer generating craters needs a density and ages");
    }
    if (options_.densityMap && !models_.density) {
        throw std::invalid_argument("A crater density map needs a density");
    }
    models_.profile = MatchedProfile(std::move(models_.profile), *models_.degradation);
    ValidateCraterSpacing(options_.generation.spacing);
    ValidateCraterApplyOptions(options_.apply);
    ValidateSourceResolvesPixels(options_.sourceResolvesPixels);
    if (auto limit = LayerResolvedLimit(source, options_.sourceResolvesPixels, options_.surface)) {
        std::shared_ptr<const SurfaceField>& completeness = options_.generation.completeness;
        completeness = completeness ? std::make_shared<const MinimumField>(completeness, limit)
                                    : std::move(limit);
    }
    options_.generation.marginRadii = CraterReachRadii(*models_.profile, options_.apply) *
                                      MaxOutlineScale(options_.apply.irregularity);
    options_.generation.surface = options_.surface;
    options_.apply.surface = options_.surface;
    cells_ = MakeCells(options_.surface);
    if (options_.cacheCraters > 0) {
        cache_ = std::make_shared<CraterCellCache>(options_.cacheCraters);
    }
    for (const SecondaryCraterOptions& secondaries : options_.secondaries) {
        ValidateSecondaryCraterOptions(secondaries);
    }
    candidates_ = models_.density;
    if (options_.densityMap) {
        FollowDensityMap();
    }
}

CraterModels DefaultCraterModels() {
    const auto production = std::make_shared<SplicedSfd>(ExtendedNeukumProductionFunction());
    const auto equilibrium = std::make_shared<PowerLawSfd>(HartmannEquilibrium());
    const Chronology chronology(NeukumLunarChronology);
    constexpr double kAgeGyr = 3.5;
    CraterModels models;
    models.density =
        std::make_shared<MinimumSfd>(std::vector<std::shared_ptr<const SizeFrequencyDistribution>>{
            std::make_shared<ProductionSurface>(production, kAgeGyr, chronology), equilibrium});
    const auto steady =
        std::make_shared<SteadyStateAges>(production, equilibrium, kAgeGyr, chronology);
    models.ages = steady;
    const FreshMorphometry fresh(LunarFreshCraterMorphometry);
    const auto profile =
        std::make_shared<DiffusedProfile>(std::make_shared<MartinParkesDunstanProfile>());
    models.degradation = std::make_shared<DiffusionDegradation>(
        fresh,
        EquilibriumDiffusionAges(
            profile, fresh,
            [steady](double diameter) { return steady->EquilibriumLifetimeGyr(diameter); }),
        profile);
    models.profile = profile;
    return models;
}

std::shared_ptr<CraterLayer> CraterLayer::Manual(CraterList craters, CraterModels models,
                                                 CraterApplyOptions apply, Surface surface) {
    CraterLayerOptions options;
    options.generateRandom = false;
    options.generation.manual = std::move(craters);
    options.apply = std::move(apply);
    options.surface = surface;
    return std::make_shared<CraterLayer>(std::move(models), std::move(options));
}

void CraterLayer::FollowDensityMap() {
    scale_ = std::make_shared<const DensityScale>(*options_.densityMap, *models_.density);
    if (scale_->Max() > 1.0) {
        candidates_ =
            std::make_shared<SummedSfd>(std::vector<WeightedSfd>{{scale_->Max(), models_.density}});
    }
    options_.generation.keepShare = [scale = scale_](const Crater& crater) {
        return scale->At(crater.x, crater.y) / scale->Max();
    };
    const DensityMap& map = *options_.densityMap;
    if (!map.total || map.measure == DensityMeasure::Scale) {
        return;
    }
    for (const SecondaryCraterOptions& secondaries : options_.secondaries) {
        throwFrom_ = std::fmin(throwFrom_, SmallestThrower(map, secondaries));
    }
    if (std::isfinite(throwFrom_) && options_.generation.maxCraters > 0) {
        throw std::invalid_argument("Taking secondaries out of a density map needs every crater, "
                                    "not the maxCraters largest");
    }
    options_.generation.spacing.outrankFrom = throwFrom_;
}

double CraterLayer::SmallestThrower(const DensityMap& map,
                                    const SecondaryCraterOptions& secondaries) {
    const double smallest = std::fmax(map.minDiameter, secondaries.minDiameter);
    const auto throws = [&](double diameter) {
        return LargestSecondary(diameter, secondaries) > smallest;
    };
    if (!throws(kLargestThrower)) {
        return std::numeric_limits<double>::infinity();
    }
    return std::fmax(BisectLog(1e-3, kLargestThrower, 100, 0.0, throws),
                     secondaries.selection.minDiameter);
}

CraterList CraterLayer::Throwers(const Bounds2d& region) const {
    CraterGenerationOptions large = options_.generation;
    large.dMin = std::fmax(large.dMin, throwFrom_);
    large.minResolvedDiameter = 0.0;
    double reach = 0.0;
    for (const SecondaryCraterOptions& secondaries : options_.secondaries) {
        reach = std::fmax(reach, SecondaryOuterRange(MaxFeatureDiameter(), secondaries));
    }
    const Bounds2d around = options_.surface.Expand(region, reach);
    if (large.dMin < ResolveDMax(large.dMax, *candidates_)) {
        return GenerateCachedCraters(cache_.get(), *cells_, around, *candidates_, *models_.ages,
                                     options_.seed, large);
    }
    return ManualCratersNear(around, 0.0);
}

CraterList CraterLayer::ManualCratersNear(const Bounds2d& region, double marginRadii) const {
    CraterList craters;
    AppendManualCraters(options_.generation.manual, craters, [&](const Crater& crater) {
        const Bounds2d near =
            marginRadii > 0.0 ? options_.surface.Expand(region, 0.5 * marginRadii * crater.diameter)
                              : region;
        return options_.surface.Contains(near, {crater.x, crater.y});
    });
    return craters;
}

CraterKeepShare CraterLayer::KeepShare(const Bounds2d& region) const {
    struct Thrower {
        Vec2d center;
        SecondaryDensity density;
    };
    auto thrown = std::make_shared<std::vector<Thrower>>();
    const CraterList throwers = Throwers(region);
    for (std::size_t n = 0; n < throwers.Size(); ++n) {
        const Crater crater = throwers.At(n);
        for (const SecondaryCraterOptions& secondaries : options_.secondaries) {
            SecondaryDensity density(crater, secondaries);
            if (density.Reach() > 0.0) {
                thrown->push_back({{crater.x, crater.y}, std::move(density)});
            }
        }
    }
    const DensityMap& map = *options_.densityMap;
    const bool cover = map.measure == DensityMeasure::ArealFraction;
    return [scale = scale_, map, cover, thrown, from = throwFrom_,
            surface = options_.surface](const Crater& crater) {
        const double share = scale->At(crater.x, crater.y) / scale->Max();
        if (crater.diameter >= from) {
            return share;
        }
        double value = MapValueAt(map, crater.x, crater.y);
        if (std::isnan(value)) {
            return share;
        }
        for (const Thrower& thrower : *thrown) {
            const Vec2d offset = surface.Offset(thrower.center, {crater.x, crater.y});
            if (std::hypot(offset.x, offset.y) < thrower.density.Reach()) {
                value -= cover ? thrower.density.CoverAt(offset, map.minDiameter, map.maxDiameter)
                               : thrower.density.At(offset, map.minDiameter, map.maxDiameter);
            }
        }
        return scale->Scale(value) / scale->Max();
    };
}

CraterList CraterLayer::Craters(const Bounds2d& region, double spacing, double minDiameter,
                                double maxDiameter) const {
    CraterGenerationOptions generation = options_.generation;
    if (options_.generateRandom) {
        if (spacing > 0.0) {
            generation.minResolvedDiameter = options_.apply.fade.startSamples * spacing;
        }
        generation.minResolvedDiameter = std::fmax(generation.minResolvedDiameter, minDiameter);
        generation.maxBandDiameter = std::fmin(generation.maxBandDiameter, maxDiameter);
        if (std::isfinite(throwFrom_)) {
            generation.keepShare = KeepShare(region);
        }
        return GenerateCachedCraters(cache_.get(), *cells_, region, *candidates_, *models_.ages,
                                     options_.seed, std::move(generation));
    }
    return ManualCratersNear(region, generation.marginRadii);
}

void CraterLayer::ApplySamples(SampleBlock& samples, const LayerContext& context) const {
    const Raster<float>* grid = samples.Grid();
    if (grid == nullptr && options_.apply.datum != CraterDatum::None) {
        RequireGround(context.ground, "Point queries of craters with a datum");
    }
    if (grid == nullptr && options_.generation.maxCraters > 0) {
        throw std::invalid_argument(
            "Point queries cannot use maxCraters, which ranks craters over a whole grid");
    }
    const double spacing = samples.ResolvedSpacing(context.spacing, options_.surface);
    const Bounds2d bounds = samples.Bounds();
    const CraterSampling sampling = SamplingFrom(context, spacing, this);
    if (grid == nullptr && options_.apply.datum != CraterDatum::None && context.cache != nullptr &&
        ApplyCratersOnCachedDatums(samples, Craters(bounds, spacing), models_.Shape(),
                                   options_.apply, sampling)) {
        return;
    }
    const double margin = DatumMargin(options_.apply, *models_.profile, MaxFeatureDiameter());
    const CraterList craters = Craters(options_.surface.Expand(bounds, margin), spacing);
    ApplyCraters(samples, craters, models_.Shape(), options_.apply, sampling);
}

CraterFeatures CraterLayer::CratersOver(const RasterGrid& grid, double spacing) const {
    return DescribeCraters(Craters(grid.Bounds(), ListedSpacing(grid, spacing, options_.surface)),
                           models_.Shape());
}

double CraterLayer::MaxFeatureDiameter() const {
    const CraterGenerationOptions& generation = options_.generation;
    double largest = options_.generateRandom ? ResolveDMax(generation.dMax, *models_.density) : 0.0;
    for (std::size_t n = 0; n < generation.manual.Size(); ++n) {
        largest = std::fmax(largest, generation.manual.At(n).diameter);
    }
    return largest;
}

}  // namespace terraingen
