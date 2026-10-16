/// @file
/// @brief Layer context checks and the default one-cell `Layer::ApplyAt`.
#include "terraingen/layers/layer.h"

#include "terraingen/util/checks.h"

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

float Layer::ApplyAt(float height, double x, double y, const LayerContext& context) const {
    ValidatePointContext(context);
    Raster<float> cell(1, 1, height);
    cell.SetTransform(GeoTransform::NorthUp(x - 0.5 * context.spacing, y + 0.5 * context.spacing,
                                            context.spacing));
    TerrainFeatures features;
    Apply(cell, context, features);
    return cell(0, 0);
}

}  // namespace terraingen
