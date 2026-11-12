// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/crater_layer.h"

#include "layers/craters/generation/cell_cache.h"
#include "layers/craters/shaping/resolved_state.h"
#include "terraingen/distribution/equilibrium.h"
#include "terraingen/distribution/neukum.h"
#include "terraingen/distribution/power_law.h"
#include "terraingen/distribution/spliced.h"
#include "terraingen/distribution/summed.h"
#include "terraingen/geometry/cells.h"
#include "terraingen/layers/craters/ages/steady_state.h"
#include "terraingen/layers/craters/crater_fields.h"
#include "terraingen/layers/craters/degradation/diffusion.h"
#include "terraingen/layers/craters/degradation/diffusivities.h"
#include "terraingen/layers/craters/generation/spacing.h"
#include "terraingen/layers/craters/profiles/martin_parkes_dunstan.h"
#include "terraingen/layers/craters/shaping/morphometry.h"
#include "terraingen/layers/source_resolved.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/grid.h"
#include "util/numerics.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// The largest primary (m) searched for the smallest one throwing secondaries a map measures,
/// about where Singer et al.'s largest secondary stops growing with the primary.
constexpr double kLargestThrower = 5e5;

/// Sets the seed of `options` and the seeds of its apply options to `ReseedWord(seed, 0)` to
/// `ReseedWord(seed, 6)`.
void ReseedCraterOptions(CraterLayerOptions& options, std::uint64_t seed) {
    options.seed = ReseedWord(seed, 0);
    CraterApplyOptions& apply = options.apply;
    apply.irregularity.seed = ReseedWord(seed, 1);
    apply.noise.seed = ReseedWord(seed, 2);
    apply.texture.seed = ReseedWord(seed, 3);
    apply.slope.seed = ReseedWord(seed, 4);
    apply.terraces.seed = ReseedWord(seed, 5);
    apply.peaks.seed = ReseedWord(seed, 6);
}

}  // namespace

CraterLayer::CraterLayer(CraterModels models, CraterLayerOptions options, TerrainBase base)
    : m_models(std::move(models)), m_options(std::move(options)),
      m_given(std::make_shared<const CraterLayerOptions>(m_options)),
      m_surface(std::move(base.surface)), m_source(std::move(base.source)) {
    if (!m_models.degradation) {
        throw std::invalid_argument("A crater layer needs a degradation model");
    }
    if (!m_models.ages) {
        m_models.ages = m_models.degradation->AgeModel();
    }
    if (m_options.generateRandom && (!m_models.density || !m_models.ages)) {
        throw std::invalid_argument("A crater layer generating craters needs a density and ages");
    }
    if (m_options.densityMap && !m_models.density) {
        throw std::invalid_argument("A crater density map needs a density");
    }
    m_models.profile = MatchedProfile(std::move(m_models.profile), *m_models.degradation);
    ValidateCraterSpacing(m_options.generation.spacing);
    ValidateCraterApplyOptions(m_options.apply);
    m_options.apply = SeededCraterApply(std::move(m_options.apply), m_options.seed);
    FollowSource();
    m_options.generation.marginRadii = CraterReachRadii(*m_models.profile, m_options.apply) *
                                       MaxOutlineScale(m_options.apply.irregularity);
    const CraterList& manual = m_options.generation.manual;
    double largestReplacing = 0.0;
    for (std::size_t n = 0; n < manual.Size(); ++n) {
        if (manual.Replace()[n] != 0) {
            largestReplacing = std::fmax(largestReplacing, manual.Diameters()[n]);
        }
    }
    m_replacementMargin = ReplacementMargin(m_options.apply, *m_models.profile, largestReplacing);
    m_options.generation.surface = m_surface;
    m_options.apply.surface = m_surface;
    m_cells = MakeCells(m_surface);
    if (m_options.cacheCraters > 0) {
        m_cache = std::make_shared<CraterCellCache>(m_options.cacheCraters);
    }
    for (const SecondaryCraterOptions& secondaries : m_options.secondaries) {
        ValidateSecondaryCraterOptions(secondaries);
    }
    m_candidates = m_models.density;
    if (m_options.densityMap) {
        FollowDensityMap();
    }
    SetUpBandCounts();
}

void CraterLayer::SetUpBandCounts() {
    const CraterGenerationOptions& generation = m_options.generation;
    if (!m_options.generateRandom || !m_candidates) {
        return;
    }
    const double maxDiameter = ResolveMaxDiameter(generation.maxDiameter, *m_candidates);
    if (std::isfinite(maxDiameter) && maxDiameter > generation.minDiameter &&
        generation.minDiameter > 0.0) {
        for (const CraterBand& band :
             CraterBands(generation.minDiameter, maxDiameter, generation.cellSizeFactor)) {
            m_bandCounts.push_back(
                {band.low, band.high, m_candidates->ExpectedCount(1.0, band.low, band.high)});
        }
    }
}

LayerPtr CraterLayer::Reseeded(std::uint64_t seed) const {
    auto layer = std::make_shared<CraterLayer>(*this);
    ReseedCraterOptions(layer->m_options, seed);
    auto given = std::make_shared<CraterLayerOptions>(*m_given);
    ReseedCraterOptions(*given, seed);
    layer->m_given = std::move(given);
    if (m_cache) {
        layer->m_cache = std::make_shared<CraterCellCache>(m_options.cacheCraters);
    }
    return layer;
}

LayerPtr CraterLayer::Rebased(const TerrainBase& base) const {
    return std::make_shared<CraterLayer>(m_models, *m_given, base);
}

void CraterLayer::CountBands(CraterGenerationOptions& generation) const {
    if (m_bandCounts.empty()) {
        return;
    }
    generation.bandCount = [counts = &m_bandCounts,
                            density = m_candidates.get()](const CraterBand& band) {
        for (const BandCount& entry : *counts) {
            if (entry.low == band.low && entry.high == band.high) {
                return entry.count;
            }
        }
        return density->ExpectedCount(1.0, band.low, band.high);
    };
}

void ValidateInheritedCraters(const InheritedCraters& inherited) {
    CheckRanges(inherited);
    if (inherited.minSourcePixels && inherited.maxSourcePixels &&
        !(*inherited.maxSourcePixels > *inherited.minSourcePixels)) {
        throw std::invalid_argument(
            "Inherited craters' max_source_px must be above their min_source_px");
    }
}

void CraterLayer::FollowSource() {
    ValidateSourceResolvesPixels(m_options.sourceResolvesPixels);
    if (auto limit = LayerResolvedLimit(m_source, m_options.sourceResolvesPixels, m_surface)) {
        std::shared_ptr<const SurfaceField>& completeness = m_options.generation.completeness;
        completeness = completeness ? std::make_shared<const MinimumField>(completeness, limit)
                                    : std::move(limit);
    }
    ValidateInheritedCraters(m_options.inherited);
    const InheritedCraters& inherited = m_options.inherited;
    if (inherited.craters.Empty()) {
        return;
    }
    const auto from = LayerResolvedLimit(m_source,
                                         inherited.minSourcePixels ? inherited.minSourcePixels
                                                                   : m_options.sourceResolvesPixels,
                                         m_surface);
    const auto upTo = LayerResolvedLimit(m_source, inherited.maxSourcePixels, m_surface);
    const CraterList stamped = FilterCraters(inherited.craters, [&](std::size_t, const Crater& c) {
        return (!from || InSource(from.get(), c.diameter, c.x, c.y)) &&
               !InSource(upTo.get(), c.diameter, c.x, c.y);
    });
    for (std::size_t n = 0; n < stamped.Size(); ++n) {
        Crater crater = stamped.At(n);
        if (!TakesRimHeight(*m_models.profile, crater.diameter)) {
            crater.rimHeight.reset();
        }
        m_options.generation.manual.Append(crater.Replace());
    }
    if (from && inherited.minSourcePixels) {
        std::shared_ptr<const SurfaceField>& completeness = m_options.generation.completeness;
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
    m_scale = std::make_shared<const DensityScale>(*m_options.densityMap, *m_models.density);
    if (m_scale->Max() > 1.0) {
        m_candidates = std::make_shared<SummedSfd>(
            std::vector<WeightedSfd>{{m_scale->Max(), m_models.density}});
    }
    m_options.generation.keepShare = [scale = m_scale](const Crater& crater) {
        return scale->At(crater.x, crater.y) / scale->Max();
    };
    const DensityMap& map = *m_options.densityMap;
    if (!map.total || map.measure == DensityMeasure::Scale) {
        return;
    }
    for (const SecondaryCraterOptions& secondaries : m_options.secondaries) {
        m_throwFrom = std::fmin(m_throwFrom, SmallestThrower(map, secondaries));
    }
    if (std::isfinite(m_throwFrom) && m_options.generation.maxCraters) {
        throw std::invalid_argument("Taking secondaries out of a density map needs every crater, "
                                    "not the maxCraters largest");
    }
    m_options.generation.spacing.outrankFrom = m_throwFrom;
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
    CraterGenerationOptions large = m_options.generation;
    large.minDiameter = std::fmax(large.minDiameter, m_throwFrom);
    large.minResolvedDiameter = 0.0;
    CountBands(large);
    double reach = 0.0;
    for (const SecondaryCraterOptions& secondaries : m_options.secondaries) {
        reach = std::fmax(reach, SecondaryOuterRange(MaxFeatureDiameter(), secondaries));
    }
    const Bounds2d around = m_surface.Expand(region, reach);
    if (large.minDiameter < ResolveMaxDiameter(large.maxDiameter, *m_candidates)) {
        return GenerateCachedCraters(m_cache.get(), *m_cells, around, *m_candidates, *m_models.ages,
                                     m_options.seed, large);
    }
    return ManualCratersNear(around, 0.0);
}

CraterList CraterLayer::ManualCratersNear(const Bounds2d& region, double marginRadii) const {
    CraterList craters;
    AppendManualCraters(m_options.generation.manual, craters, [&](const Crater& crater) {
        const Bounds2d near = marginRadii > 0.0
                                  ? m_surface.Expand(region, 0.5 * marginRadii * crater.diameter)
                                  : region;
        return m_surface.Contains(near, {crater.x, crater.y});
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
        for (const SecondaryCraterOptions& secondaries : m_options.secondaries) {
            SecondaryDensity density(crater, secondaries);
            if (density.Reach() > 0.0) {
                thrown->push_back({{crater.x, crater.y}, std::move(density)});
            }
        }
    }
    const DensityMap& map = *m_options.densityMap;
    const bool cover = map.measure == DensityMeasure::ArealFraction;
    return [scale = m_scale, map, cover, thrown, from = m_throwFrom,
            surface = m_surface](const Crater& crater) {
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
    CraterGenerationOptions generation = m_options.generation;
    generation.threads = threads;
    if (m_options.generateRandom) {
        if (spacing > 0.0) {
            generation.minResolvedDiameter = m_options.apply.fade.startSamples * spacing;
        }
        generation.minResolvedDiameter = std::fmax(generation.minResolvedDiameter, minDiameter);
        generation.maxBandDiameter = std::fmin(generation.maxBandDiameter, maxDiameter);
        if (std::isfinite(m_throwFrom)) {
            generation.keepShare = KeepShare(region);
        }
        CountBands(generation);
        return GenerateCachedCraters(m_cache.get(), *m_cells, region, *m_candidates, *m_models.ages,
                                     m_options.seed, std::move(generation));
    }
    return ManualCratersNear(region, generation.marginRadii);
}

void CraterLayer::ApplySamples(SampleBlock& samples, const LayerContext& context) const {
    const Raster<float>* grid = samples.Grid();
    if (grid == nullptr && m_options.apply.datum != CraterDatum::None) {
        RequireGround(context.ground, "Point queries of craters with a datum");
    }
    if (grid == nullptr && m_replacementMargin > 0.0) {
        RequireGround(context.ground, "Point queries of replacing craters");
    }
    if (grid == nullptr && m_options.generation.maxCraters) {
        throw std::invalid_argument(
            "Point queries cannot use maxCraters, which ranks craters over a whole grid");
    }
    const double spacing = samples.ResolvedSpacing(context.spacing, m_surface);
    const Bounds2d bounds = samples.Bounds();
    const CraterSampling sampling = SamplingFrom(context, spacing, this);
    if (grid == nullptr && m_options.apply.datum != CraterDatum::None && context.cache != nullptr &&
        ApplyCratersOnCachedDatums(samples, Craters(bounds, spacing), m_models.Shape(),
                                   m_options.apply, sampling)) {
        return;
    }
    const CraterList craters =
        Craters(m_surface.Expand(bounds, GroundReach(spacing)), spacing, 0.0,
                std::numeric_limits<double>::infinity(), grid != nullptr ? context.threads : 1);
    ApplyCraters(samples, craters, m_models.Shape(), m_options.apply, sampling);
}

double CraterLayer::GroundReach(double /*spacing*/) const {
    return std::fmax(DatumMargin(m_options.apply, *m_models.profile, MaxFeatureDiameter()),
                     m_replacementMargin);
}

CraterFeatures CraterLayer::CratersOver(const RasterGrid& grid, double spacing) const {
    return DescribeCraters(Craters(grid.Bounds(), ListedSpacing(grid, spacing, m_surface)),
                           m_models.Shape());
}

double CraterLayer::MaxFeatureDiameter() const {
    const CraterGenerationOptions& generation = m_options.generation;
    double largest = m_options.generateRandom
                         ? ResolveMaxDiameter(generation.maxDiameter, *m_models.density)
                         : 0.0;
    for (std::size_t n = 0; n < generation.manual.Size(); ++n) {
        largest = std::fmax(largest, generation.manual.At(n).diameter);
    }
    return largest;
}

}  // namespace terraingen
