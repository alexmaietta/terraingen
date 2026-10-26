// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/crater_layer.h"

#include "layers/craters/generation/cell_cache.h"
#include "layers/craters/shaping/resolved_state.h"
#include "terraingen/distribution/equilibrium.h"
#include "terraingen/distribution/neukum.h"
#include "terraingen/distribution/power_law.h"
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
#include "terraingen/util/cells.h"
#include "util/numerics.h"

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

CraterLayer::CraterLayer(CraterModels models, CraterLayerOptions options, TerrainBase base)
    : models_(std::move(models)), options_(std::move(options)), surface_(base.surface) {
    if (!models_.degradation) {
        throw std::invalid_argument("A crater layer needs a degradation model");
    }
    if (!models_.ages) {
        models_.ages = models_.degradation->AgeModel();
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
    options_.apply = SeededCraterApply(std::move(options_.apply), options_.seed);
    ValidateSourceResolvesPixels(options_.sourceResolvesPixels);
    if (auto limit = LayerResolvedLimit(base.source, options_.sourceResolvesPixels, surface_)) {
        std::shared_ptr<const SurfaceField>& completeness = options_.generation.completeness;
        completeness = completeness ? std::make_shared<const MinimumField>(completeness, limit)
                                    : std::move(limit);
    }
    ValidateInheritedCraters(options_.inherited);
    Inherit(base.source);
    options_.generation.marginRadii = CraterReachRadii(*models_.profile, options_.apply) *
                                      MaxOutlineScale(options_.apply.irregularity);
    const CraterList& manual = options_.generation.manual;
    double largestReplacing = 0.0;
    for (std::size_t n = 0; n < manual.Size(); ++n) {
        if (manual.Replace()[n] != 0) {
            largestReplacing = std::fmax(largestReplacing, manual.Diameters()[n]);
        }
    }
    replacementMargin_ = ReplacementMargin(options_.apply, *models_.profile, largestReplacing);
    options_.generation.surface = surface_;
    options_.apply.surface = surface_;
    cells_ = MakeCells(surface_);
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
    const CraterGenerationOptions& generation = options_.generation;
    if (!options_.generateRandom || !candidates_) {
        return;
    }
    const double dMax = ResolveDMax(generation.dMax, *candidates_);
    if (std::isfinite(dMax) && dMax > generation.dMin && generation.dMin > 0.0) {
        for (const CraterBand& band :
             CraterBands(generation.dMin, dMax, generation.cellSizeFactor)) {
            bandCounts_.push_back(
                {band.low, band.high, candidates_->ExpectedCount(1.0, band.low, band.high)});
        }
    }
}

void CraterLayer::CountBands(CraterGenerationOptions& generation) const {
    if (bandCounts_.empty()) {
        return;
    }
    generation.bandCount = [counts = &bandCounts_,
                            density = candidates_.get()](const CraterBand& band) {
        for (const BandCount& entry : *counts) {
            if (entry.low == band.low && entry.high == band.high) {
                return entry.count;
            }
        }
        return density->ExpectedCount(1.0, band.low, band.high);
    };
}

void ValidateInheritedCraters(const InheritedCraters& inherited) {
    ValidateSourceResolvesPixels(inherited.minSourcePixels);
    ValidateSourceResolvesPixels(inherited.maxSourcePixels);
    if (inherited.minSourcePixels && inherited.maxSourcePixels &&
        !(*inherited.maxSourcePixels > *inherited.minSourcePixels)) {
        throw std::invalid_argument(
            "Inherited craters' maxSourcePixels must be above their minSourcePixels");
    }
}

void CraterLayer::Inherit(const std::shared_ptr<const Source>& source) {
    const InheritedCraters& inherited = options_.inherited;
    if (inherited.craters.Empty()) {
        return;
    }
    const auto from = LayerResolvedLimit(source,
                                         inherited.minSourcePixels ? inherited.minSourcePixels
                                                                   : options_.sourceResolvesPixels,
                                         surface_);
    const auto upTo = LayerResolvedLimit(source, inherited.maxSourcePixels, surface_);
    const CraterList stamped = FilterCraters(inherited.craters, [&](std::size_t, const Crater& c) {
        return (!from || InSource(from.get(), c.diameter, c.x, c.y)) &&
               !InSource(upTo.get(), c.diameter, c.x, c.y);
    });
    for (std::size_t n = 0; n < stamped.Size(); ++n) {
        Crater crater = stamped.At(n);
        if (!TakesRimHeight(*models_.profile, crater.diameter)) {
            crater.rimHeight.reset();
        }
        options_.generation.manual.Append(crater.Replace());
    }
    if (from && inherited.minSourcePixels) {
        std::shared_ptr<const SurfaceField>& completeness = options_.generation.completeness;
        completeness =
            completeness ? std::make_shared<const MinimumField>(completeness, from) : from;
    }
}

CraterModels DefaultCraterModels() {
    const EquilibriumSurface surface{
        std::make_shared<SplicedSfd>(ExtendedNeukumProductionFunction()),
        std::make_shared<PowerLawSfd>(HartmannEquilibrium()), 3.5, NeukumLunarChronology};
    const auto ages = surface.Ages();
    const auto profile =
        std::make_shared<DiffusedProfile>(std::make_shared<MartinParkesDunstanProfile>());
    return {surface.Density(), ages,
            MatchedDiffusionDegradation(profile, LunarFreshCraterMorphometry, ages), profile};
}

std::shared_ptr<CraterLayer> CraterLayer::Manual(CraterList craters, CraterModels models,
                                                 CraterApplyOptions apply, TerrainBase base) {
    CraterLayerOptions options;
    options.generateRandom = false;
    options.generation.manual = std::move(craters);
    options.apply = std::move(apply);
    return std::make_shared<CraterLayer>(std::move(models), std::move(options), std::move(base));
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
    if (std::isfinite(throwFrom_) && options_.generation.maxCraters) {
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
    CountBands(large);
    double reach = 0.0;
    for (const SecondaryCraterOptions& secondaries : options_.secondaries) {
        reach = std::fmax(reach, SecondaryOuterRange(MaxFeatureDiameter(), secondaries));
    }
    const Bounds2d around = surface_.Expand(region, reach);
    if (large.dMin < ResolveDMax(large.dMax, *candidates_)) {
        return GenerateCachedCraters(cache_.get(), *cells_, around, *candidates_, *models_.ages,
                                     options_.seed, large);
    }
    return ManualCratersNear(around, 0.0);
}

CraterList CraterLayer::ManualCratersNear(const Bounds2d& region, double marginRadii) const {
    CraterList craters;
    AppendManualCraters(options_.generation.manual, craters, [&](const Crater& crater) {
        const Bounds2d near = marginRadii > 0.0
                                  ? surface_.Expand(region, 0.5 * marginRadii * crater.diameter)
                                  : region;
        return surface_.Contains(near, {crater.x, crater.y});
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
            surface = surface_](const Crater& crater) {
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
                                double maxDiameter, unsigned threads) const {
    CraterGenerationOptions generation = options_.generation;
    generation.threads = threads;
    if (options_.generateRandom) {
        if (spacing > 0.0) {
            generation.minResolvedDiameter = options_.apply.fade.startSamples * spacing;
        }
        generation.minResolvedDiameter = std::fmax(generation.minResolvedDiameter, minDiameter);
        generation.maxBandDiameter = std::fmin(generation.maxBandDiameter, maxDiameter);
        if (std::isfinite(throwFrom_)) {
            generation.keepShare = KeepShare(region);
        }
        CountBands(generation);
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
    if (grid == nullptr && replacementMargin_ > 0.0) {
        RequireGround(context.ground, "Point queries of replacing craters");
    }
    if (grid == nullptr && options_.generation.maxCraters) {
        throw std::invalid_argument(
            "Point queries cannot use maxCraters, which ranks craters over a whole grid");
    }
    const double spacing = samples.ResolvedSpacing(context.spacing, surface_);
    const Bounds2d bounds = samples.Bounds();
    const CraterSampling sampling = SamplingFrom(context, spacing, this);
    if (grid == nullptr && options_.apply.datum != CraterDatum::None && context.cache != nullptr &&
        ApplyCratersOnCachedDatums(samples, Craters(bounds, spacing), models_.Shape(),
                                   options_.apply, sampling)) {
        return;
    }
    const double margin = std::fmax(
        DatumMargin(options_.apply, *models_.profile, MaxFeatureDiameter()), replacementMargin_);
    const CraterList craters =
        Craters(surface_.Expand(bounds, margin), spacing, 0.0,
                std::numeric_limits<double>::infinity(), grid != nullptr ? context.threads : 1);
    ApplyCraters(samples, craters, models_.Shape(), options_.apply, sampling);
}

CraterFeatures CraterLayer::CratersOver(const RasterGrid& grid, double spacing) const {
    return DescribeCraters(Craters(grid.Bounds(), ListedSpacing(grid, spacing, surface_)),
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
