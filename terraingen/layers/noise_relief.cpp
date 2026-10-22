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

}  // namespace terraingen
