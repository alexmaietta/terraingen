/// @file
/// @brief `CraterLayer`: generating craters around grids and points and applying them.
#include "terraingen/layers/craters/crater_layer.h"

#include "terraingen/layers/craters/generation/spacing.h"
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

void CraterLayer::ApplySamples(SampleBlock& samples, const LayerContext& context) const {
    const Raster<float>* grid = samples.Grid();
    if (grid == nullptr && options_.apply.datum != CraterDatum::None) {
        RequireGround(context.ground, "Point queries of craters with a datum");
    }
    if (grid == nullptr && options_.generation.maxCraters > 0) {
        throw std::invalid_argument(
            "Point queries cannot use maxCraters, which ranks craters over a whole grid");
    }
    const double spacing =
        grid != nullptr ? std::max(context.spacing, RasterGrid::Of(*grid).Spacing(options_.surface))
                        : context.spacing;
    const Bounds2d bounds = samples.Bounds();
    const double margin = DatumMargin(options_.apply, *models_.profile, MaxFeatureDiameter());
    const CraterList craters = Craters(options_.surface.Expand(bounds, margin), spacing);
    const HeightSampler ground =
        context.ground ? context.ground->Sampler(spacing) : HeightSampler{};
    ApplyCraters(samples, craters, models_.Shape(), options_.apply,
                 {spacing, context.threads, ground});
}

CraterAttributes CraterLayer::Attributes(const Crater& crater) const {
    return ComputeCraterAttributes(crater, models_.degradation->State(crater), *models_.profile);
}

CraterFeatures CraterLayer::CratersOver(const RasterGrid& grid, double spacing) const {
    CraterFeatures placed{Craters(grid.Bounds(), std::max(spacing, grid.Spacing(options_.surface))),
                          {}};
    for (std::size_t n = 0; n < placed.craters.Size(); ++n) {
        placed.attributes.push_back(Attributes(placed.craters.At(n)));
    }
    return placed;
}

double CraterLayer::MaxFeatureDiameter() const {
    const CraterGenerationOptions& generation = options_.generation;
    double largest = options_.generate ? ResolveDMax(generation.dMax, *models_.density) : 0.0;
    for (std::size_t n = 0; n < generation.manual.Size(); ++n) {
        largest = std::fmax(largest, generation.manual.At(n).diameter);
    }
    return largest;
}

}  // namespace terraingen
