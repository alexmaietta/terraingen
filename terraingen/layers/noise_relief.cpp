#include "terraingen/layers/noise_relief.h"

#include "terraingen/layers/surface_noise.h"

#include <stdexcept>
#include <utility>

namespace terraingen {

NoiseRelief::NoiseRelief(SyntheticTerrainParams params, Vec2d origin, Surface surface)
    : FormulaLayer(surface), params_(std::move(params)), origin_(origin) {
    ValidateSyntheticTerrain(params_);
    if (surface_.IsSphere()) {
        for (const NoiseLayer& layer : params_.layers) {
            if (!HasBasisNoise3(layer.fractal.basis)) {
                throw std::invalid_argument(
                    "Noise on a sphere needs a basis with a 3D form: perlin or value");
            }
        }
    }
}

float NoiseRelief::HeightAt(float height, double x, double y, double /*spacing*/,
                            const Ground* /*ground*/) const {
    return height + NoiseLayersAt(params_.layers, origin_, surface_, x, y);
}

}  // namespace terraingen
