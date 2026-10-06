#include "terraingen/layers/craters/crater_layer.h"

#include "terraingen/layers/craters/filters.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/cells.h"

#include <algorithm>
#include <cmath>
#include <limits>
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
    options_.generation.marginRadii = models_.profile->ReachRadii();
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
            generation.cellCraters = [this](const CraterBand& band, const CellIndex& cell) {
                return cache_->Get(band.exponent, cell, [&] {
                    return GenerateCellCraters(PlanarCells(), band, cell, *models_.density,
                                               *models_.ages, options_.seed);
                });
            };
        }
        return GenerateCraters(PlanarCells(), region, *models_.density, *models_.ages,
                               options_.seed, generation);
    }
    CraterList craters;
    for (std::size_t n = 0; n < generation.manual.Size(); ++n) {
        Crater crater = generation.manual.At(n);
        crater.manual = true;
        if (crater.id == 0) {
            crater.id = ManualCraterId(crater.x, crater.y, crater.diameter);
        }
        const double margin = 0.5 * generation.marginRadii * crater.diameter;
        if (region.Expanded(margin).Contains(crater.x, crater.y)) {
            craters.Append(crater);
        }
    }
    return craters;
}

void CraterLayer::Apply(Raster<float>& heights, const LayerContext& context,
                        TerrainFeatures& features) const {
    const RasterGrid grid = RasterGrid::Of(heights);
    const double resolved = std::max(context.spacing, grid.PixelSize());
    const Bounds2d bounds = grid.Bounds();
    const double margin = DatumMargin(options_.apply, *models_.profile, MaxFeatureDiameter());
    const CraterList craters = Craters(bounds.Expanded(margin), resolved);
    const HeightSampler ground =
        context.ground ? context.ground->Sampler(resolved) : HeightSampler{};
    ApplyCraters(heights, craters, *models_.profile, *models_.degradation, options_.apply, resolved,
                 context.threads, ground);
    features.craters.Append(margin > 0.0 ? Craters(bounds, resolved) : craters);
}

double CraterLayer::MaxFeatureDiameter() const {
    const CraterGenerationOptions& generation = options_.generation;
    double largest = 0.0;
    if (options_.generate) {
        largest = std::isinf(generation.dMax) ? models_.density->ValidRange().max : generation.dMax;
    }
    for (std::size_t n = 0; n < generation.manual.Size(); ++n) {
        largest = std::fmax(largest, generation.manual.At(n).diameter);
    }
    return largest;
}

float CraterLayer::ApplyAt(float height, double x, double y, const LayerContext& context) const {
    const double spacing = context.spacing;
    if (!(std::isfinite(spacing) && spacing > 0.0)) {
        throw std::invalid_argument("Sample spacing must be positive and finite");
    }
    const bool datum = options_.apply.datum != CraterDatum::None;
    if (datum && context.ground == nullptr) {
        throw std::invalid_argument(
            "Point queries of craters with a datum need the ground beneath the layer");
    }
    if (options_.generation.maxCraters > 0) {
        throw std::invalid_argument(
            "Point queries cannot use maxCraters, which ranks craters over a whole grid");
    }
    if (std::isnan(height)) {
        return height;
    }
    constexpr double kInf = std::numeric_limits<double>::infinity();
    const double margin = DatumMargin(options_.apply, *models_.profile, MaxFeatureDiameter());
    const Bounds2d point =
        Bounds2d{x, y, std::nextafter(x, kInf), std::nextafter(y, kInf)}.Expanded(margin);
    const CraterList craters = Craters(point, spacing);
    const CraterProfile& profile = *models_.profile;
    std::vector<CraterInfluence::Entry> resolvedCraters;
    for (const std::size_t index : CraterApplyOrder(craters, options_.apply.order)) {
        const Crater crater = craters.At(index);
        const double resolution = ResolutionWeight(crater.diameter, spacing, options_.apply.fade);
        if (resolution > 0.0) {
            resolvedCraters.push_back({crater, resolution, models_.degradation->State(crater)});
        }
    }
    const CraterInfluence influence(std::move(resolvedCraters), profile);
    const HeightSampler ground = datum ? context.ground->Sampler(spacing) : HeightSampler{};
    const auto& entries = influence.Entries();
    for (std::size_t k = 0; k < entries.size(); ++k) {
        const CraterInfluence::Entry& entry = entries[k];
        const double dx = x - entry.crater.x;
        const double dy = y - entry.crater.y;
        if (std::sqrt(dx * dx + dy * dy) >= profile.ReachRadii() * 0.5 * entry.crater.diameter) {
            continue;
        }
        const auto fitted =
            datum ? FitCraterDatum(entry.crater, options_.apply, DatumGround(ground, influence, k))
                  : std::nullopt;
        height = ApplyCraterTo(height, x, y, entry.crater, entry.state, entry.resolution, fitted,
                               profile, options_.apply);
    }
    return height;
}

}  // namespace terraingen
