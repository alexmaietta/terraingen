#include "terraingen/layers/layer.h"

#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace terraingen {

void ValidatePointContext(const LayerContext& context) {
    RequirePositive(context.spacing, "Sample spacing");
}

void RequireGround(const Ground* ground, std::string_view what) {
    if (ground == nullptr) {
        throw std::invalid_argument(std::string(what) + " need the ground");
    }
}

void Layer::Apply(Raster<float>& heights, const LayerContext& context) const {
    SampleBlock samples(heights);
    ApplySamples(samples, context);
}

void Layer::Apply(SampleBlock& samples, const LayerContext& context) const {
    if (!samples.IsGrid()) {
        ValidatePointContext(context);
    }
    ApplySamples(samples, context);
}

float Layer::ApplyAt(float height, double x, double y, const LayerContext& context) const {
    const Vec2d position{x, y};
    SampleBlock samples({&position, 1}, {&height, 1});
    Apply(samples, context);
    return height;
}

void FormulaLayer::ApplySamples(SampleBlock& samples, const LayerContext& context) const {
    CheckContext(context);
    if (Raster<float>* grid = samples.Grid()) {
        const double spacing = std::max(context.spacing, RasterGrid::Of(*grid).Spacing(surface_));
        MapCells(*grid, context.threads, [&](float height, const Vec2d& centre) {
            return HeightAt(height, centre.x, centre.y, spacing, context.ground);
        });
        return;
    }
    for (std::size_t i = 0; i < samples.Size(); ++i) {
        if (!samples.IsNoData(i)) {
            const Vec2d p = samples.Position(i);
            samples.Height(i) =
                HeightAt(samples.Height(i), p.x, p.y, context.spacing, context.ground);
        }
    }
}

}  // namespace terraingen
