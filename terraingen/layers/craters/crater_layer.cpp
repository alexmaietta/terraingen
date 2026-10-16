/// @file
/// @brief `CraterLayer`: generating craters around grids and points and applying them.
#include "terraingen/layers/craters/crater_layer.h"

#include "terraingen/layers/craters/spacing.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/cells.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terraingen {

CraterLayer::CraterLayer(CraterModels models, CraterLayerOptions options)
    : models_(std::move(models)), options_(std::move(options)) {
    if (!models_.density || !models_.ages || !models_.degradation || !models_.profile) {
        throw std::invalid_argument(
            "A crater layer needs a density, ages, degradation and profile");
    }
    ValidateCraterSpacing(options_.generation.spacing);
    ValidateCraterApplyOptions(options_.apply);
    options_.generation.marginRadii =
        models_.profile->ReachRadii() * MaxOutlineScale(options_.apply.irregularity);
    options_.generation.surface = options_.surface;
    options_.apply.surface = options_.surface;
    if (options_.surface.IsSphere()) {
        cells_ = std::make_shared<CubeSphereCells>(options_.surface.Radius());
    } else {
        cells_ = std::make_shared<PlanarCells>();
    }
    if (options_.cacheCraters > 0) {
        cache_ = std::make_shared<CraterCellCache>(options_.cacheCraters);
    }
}

CraterList CraterLayer::Craters(const Bounds2d& region, double spacing) const {
    CraterGenerationOptions generation = options_.generation;
    if (options_.generate) {
        if (spacing > 0.0) {
            generation.minResolvedDiameter = options_.apply.fade.startSamples * spacing;
        }
        if (cache_) {
            generation.cellCraters =
                CachedCellCraters(*cache_, *cells_, *models_.density, *models_.ages, options_.seed);
        }
        return GenerateCraters(*cells_, region, *models_.density, *models_.ages, options_.seed,
                               generation);
    }
    CraterList craters;
    AppendManualCraters(generation.manual, craters, [&](const Crater& crater) {
        const double margin = 0.5 * generation.marginRadii * crater.diameter;
        return options_.surface.Contains(options_.surface.Expand(region, margin),
                                         {crater.x, crater.y});
    });
    return craters;
}

void CraterLayer::ApplySamples(SampleBlock& samples, const LayerContext& context,
                               TerrainFeatures& features) const {
    if (Raster<float>* grid = samples.Grid()) {
        ApplyToGrid(*grid, context, features);
        return;
    }
    if (options_.apply.datum != CraterDatum::None) {
        RequireGround(context.ground, "Point queries of craters with a datum");
    }
    if (options_.generation.maxCraters > 0) {
        throw std::invalid_argument(
            "Point queries cannot use maxCraters, which ranks craters over a whole grid");
    }
    for (std::size_t i = 0; i < samples.Size(); ++i) {
        if (!samples.IsNoData(i)) {
            const Vec2d p = samples.Position(i);
            samples.Height(i) = HeightAtPoint(samples.Height(i), p.x, p.y, context);
        }
    }
}

void CraterLayer::ApplyToGrid(Raster<float>& heights, const LayerContext& context,
                              TerrainFeatures& features) const {
    const RasterGrid grid = RasterGrid::Of(heights);
    const double resolved = std::max(context.spacing, grid.Spacing(options_.surface));
    const Bounds2d bounds = grid.Bounds();
    const double margin = DatumMargin(options_.apply, *models_.profile, MaxFeatureDiameter());
    const CraterList craters = Craters(options_.surface.Expand(bounds, margin), resolved);
    const HeightSampler ground =
        context.ground ? context.ground->Sampler(resolved) : HeightSampler{};
    ApplyCraters(heights, craters, models_.Shape(), options_.apply,
                 {resolved, context.threads, ground});
    const CraterList recorded = margin > 0.0 ? Craters(bounds, resolved) : craters;
    features.AddCraters(recorded, [&](const Crater& crater) { return Attributes(crater); });
}

CraterAttributes CraterLayer::Attributes(const Crater& crater) const {
    return ComputeCraterAttributes(crater, models_.degradation->State(crater), *models_.profile);
}

double CraterLayer::MaxFeatureDiameter() const {
    const CraterGenerationOptions& generation = options_.generation;
    double largest = options_.generate ? ResolveDMax(generation.dMax, *models_.density) : 0.0;
    for (std::size_t n = 0; n < generation.manual.Size(); ++n) {
        largest = std::fmax(largest, generation.manual.At(n).diameter);
    }
    return largest;
}

float CraterLayer::HeightAtPoint(float height, double x, double y,
                                 const LayerContext& context) const {
    const bool datum = options_.apply.datum != CraterDatum::None;
    const double spacing = context.spacing;
    const CraterProfile& profile = *models_.profile;
    const double margin = DatumMargin(options_.apply, profile, MaxFeatureDiameter());
    const CraterList craters = Craters(options_.surface.Expand(PointBounds(x, y), margin), spacing);
    std::vector<PreparedCrater> prepared =
        PrepareCraters(craters, options_.apply, spacing, *models_.degradation);
    std::optional<CraterReliefIndex> index;
    HeightSampler ground;
    if (datum) {
        index.emplace(prepared, profile, MaxOutlineScale(options_.apply.irregularity),
                      options_.surface);
        ground = context.ground->Sampler(spacing);
    }
    for (std::size_t k = 0; k < prepared.size(); ++k) {
        PreparedCrater& crater = prepared[k];
        if (CraterProfileDistance(crater.crater, crater.outline, x, y, options_.surface) >=
            profile.ReachRadii() * 0.5 * crater.crater.diameter) {
            continue;
        }
        if (index) {
            crater.datum =
                FitCraterDatum(crater.crater, options_.apply, DatumGround(ground, *index, k));
        }
        height = ApplyCraterTo(height, x, y, crater, profile, options_.apply);
    }
    return height;
}

}  // namespace terraingen
