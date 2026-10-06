#include "terraingen/layers/craters/crater_layer.h"

#include "terraingen/layers/craters/filters.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/cells.h"

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
}

CraterList CraterLayer::Craters(const Bounds2d& region) const {
    const CraterGenerationOptions& generation = options_.generation;
    if (options_.generate) {
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
    const RasterGrid grid = RasterGrid::Of(heights);
    const CraterList craters = Craters(grid.Bounds());
    CraterApplyOptions apply = options_.apply;
    const double pixelSize = grid.PixelSize();
    if (spacing > pixelSize) {
        apply.minDiameterPixels *= spacing / pixelSize;
    }
    ApplyCraters(heights, craters, *models_.profile, *models_.degradation, apply);
    features.craters.Append(craters);
}

}  // namespace terraingen
