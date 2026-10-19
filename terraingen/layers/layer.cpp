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
    const double spacing = samples.ResolvedSpacing(context.spacing, surface_);
    MapSamples(samples, context.threads, [&](float height, const Vec2d& p) {
        return HeightAt(height, p.x, p.y, spacing, context.ground);
    });
}

}  // namespace terraingen
