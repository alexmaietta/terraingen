#include "terraingen/layers/noise_relief.h"

#include <stdexcept>
#include <utility>

namespace terraingen {

NoiseRelief::NoiseRelief(SyntheticTerrainParams params, Vec2d origin, Surface surface)
    : FormulaLayer(surface), params_(std::move(params)), origin_(origin) {
    ValidateSyntheticTerrain(params_);
}

float NoiseRelief::HeightAt(float height, double x, double y, double /*spacing*/,
                            const Ground* /*ground*/) const {
    return height + NoiseLayersAt(params_.layers, origin_, surface_, x, y);
}

float NoiseLayersAt(const std::vector<NoiseLayer>& layers, const Vec2d& origin,
                    const Surface& surface, double x, double y) {
    if (surface.IsBody()) {
        const Vec3d p = surface.Position({x, y});
        return EvaluateNoiseLayers3(p.x, p.y, p.z, layers.data(), layers.size());
    }
    return EvaluateNoiseLayers(static_cast<float>(x - origin.x), static_cast<float>(y - origin.y),
                               layers.data(), layers.size());
}

}  // namespace terraingen
