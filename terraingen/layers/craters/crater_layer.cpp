#include "terraingen/layers/craters/crater_layer.h"

#include "terraingen/layers/craters/filters.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/cells.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

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

void CraterLayer::Apply(Raster<float>& heights, double spacing, TerrainFeatures& features) const {
    ApplyParallel(heights, spacing, features, 1);
}

void CraterLayer::ApplyParallel(Raster<float>& heights, double spacing, TerrainFeatures& features,
                                unsigned threads) const {
    const RasterGrid grid = RasterGrid::Of(heights);
    const double resolved = std::max(spacing, grid.PixelSize());
    const CraterList craters = Craters(grid.Bounds(), resolved);
    ApplyCraters(heights, craters, *models_.profile, *models_.degradation, options_.apply, resolved,
                 threads);
    features.craters.Append(craters);
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

float CraterLayer::ApplyAt(float height, double x, double y, double spacing) const {
    if (!(std::isfinite(spacing) && spacing > 0.0)) {
        throw std::invalid_argument("Sample spacing must be positive and finite");
    }
    if (options_.apply.datum != CraterDatum::None) {
        throw std::invalid_argument(
            "Point queries need additive craters; a datum needs the heights around the point");
    }
    if (options_.generation.maxCraters > 0) {
        throw std::invalid_argument(
            "Point queries cannot use maxCraters, which ranks craters over a whole grid");
    }
    if (std::isnan(height)) {
        return height;
    }
    constexpr double kInf = std::numeric_limits<double>::infinity();
    const Bounds2d point{x, y, std::nextafter(x, kInf), std::nextafter(y, kInf)};
    const CraterList craters = Craters(point, spacing);
    const CraterProfile& profile = *models_.profile;
    for (const std::size_t index : CraterApplyOrder(craters, options_.apply.order)) {
        const Crater crater = craters.At(index);
        const double resolution = ResolutionWeight(crater.diameter, spacing, options_.apply.fade);
        if (resolution == 0.0) {
            continue;
        }
        const double radius = 0.5 * crater.diameter;
        const double dx = x - crater.x;
        const double dy = y - crater.y;
        const double distance = std::sqrt(dx * dx + dy * dy);
        if (distance >= profile.ReachRadii() * radius) {
            continue;
        }
        const double replaced = 0.0;
        height =
            static_cast<float>(static_cast<double>(height) +
                               resolution * profile.Height(distance, crater.diameter,
                                                           models_.degradation->State(crater)) +
                               resolution * replaced);
    }
    return height;
}

}  // namespace terraingen
