#include "terraingen/layers/noise_relief.h"

#include "terraingen/layers/kernel.h"
#include "terraingen/raster/cell_loops.h"

#include <cstddef>
#include <utility>

namespace terraingen {

NoiseRelief::NoiseRelief(SyntheticTerrainParams params, Vec2d origin, Surface surface)
    : params_(std::move(params)), origin_(origin), surface_(surface) {
    ValidateSyntheticTerrain(params_);
    kernel_ = MakeNoiseReliefKernel(params_.layers);
}

void NoiseRelief::ApplySamples(SampleBlock& samples, const LayerContext& context) const {
    if (!surface_.IsBody()) {
        ApplyKernel(samples, context, kernel_, origin_);
        return;
    }
    MapSamples(samples, context.threads, [&](float height, const Vec2d& p) {
        return height + NoiseLayersAt(params_.layers, origin_, surface_, p.x, p.y);
    });
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
