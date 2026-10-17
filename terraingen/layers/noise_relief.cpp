#include "terraingen/layers/noise_relief.h"

#include "terraingen/layers/cell_kernels.h"
#include "terraingen/raster/cell_loops.h"

#include <cstddef>
#include <utility>

extern "C" {
extern const unsigned char terraingen_kernel_noise_relief[];
extern const std::size_t terraingen_kernel_noise_relief_size;
}

namespace terraingen {

NoiseRelief::NoiseRelief(SyntheticTerrainParams params, Vec2d origin, Surface surface)
    : params_(std::move(params)), origin_(origin), surface_(surface) {
    ValidateSyntheticTerrain(params_);
    kernel_ = MakeNoiseReliefKernel(params_.layers);
}

void NoiseRelief::ApplySamples(SampleBlock& samples, const LayerContext& context) const {
    if (!surface_.IsBody()) {
        ApplyCellKernel(samples, context, kernel_, origin_,
                        {terraingen_kernel_noise_relief, terraingen_kernel_noise_relief_size});
        return;
    }
    const auto at = [&](float height, const Vec2d& p) {
        return height + NoiseLayersAt(params_.layers, origin_, surface_, p.x, p.y);
    };
    if (Raster<float>* grid = samples.Grid()) {
        MapCells(*grid, context.threads, at);
        return;
    }
    for (std::size_t i = 0; i < samples.Size(); ++i) {
        if (!samples.IsNoData(i)) {
            samples.Height(i) = at(samples.Height(i), samples.Position(i));
        }
    }
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
