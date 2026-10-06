#pragma once

#include "terraingen/noise/kernels/fractal.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace terraingen {

/// How a noise layer combines with the height accumulated from the layers before it.
enum class LayerBlend : std::uint32_t {
    Add = 0,   ///< height + layer
    Max = 1,   ///< max(height, layer)
    Min = 2,   ///< min(height, layer)
    Mask = 3,  ///< height * (layer / amplitude + 1) / 2: the layer modulates roughness by region.
};

inline constexpr std::uint32_t kLayerBlendCount = 4;

/// Most layers a synthetic terrain may stack; a fixed bound keeps layers uploadable to GPUs.
inline constexpr std::size_t kMaxNoiseLayers = 8;

/// One layer of a synthetic terrain: a fractal, an optional domain warp and a blend mode.
struct NoiseLayer {
    bool enabled = true;
    FractalParams fractal;
    LayerBlend blend = LayerBlend::Add;
    float warpAmplitude = 0.0f;     ///< Domain warp displacement in metres; 0 disables warping.
    float warpWavelength = 500.0f;  ///< Wavelength of the warp field in metres.

    bool operator==(const NoiseLayer&) const = default;
};

/// Domain warp offset at (`x`, `y`): two independent 4-octave Perlin fBm fields of
/// `layer.warpWavelength`, each bounded by `layer.warpAmplitude` metres.
///
/// Reference: I. Quilez, "Domain Warping", 2002. https://iquilezles.org/articles/warp/
inline void DomainWarp2(float x, float y, const NoiseLayer& layer, float& dx, float& dy) {
    FractalParams warp;
    warp.wavelength = layer.warpWavelength;
    warp.amplitude = layer.warpAmplitude;
    warp.octaves = 4;
    warp.seed = PcgHash(layer.fractal.seed ^ 0x5bd1e995u);
    dx = Fractal2(x, y, warp);
    warp.seed = PcgHash(warp.seed);
    dy = Fractal2(x, y, warp);
}

/// Normalized value of `layer` at (`x`, `y`), in [-1, 1], before amplitude and blending.
inline float EvaluateNoiseLayer(float x, float y, const NoiseLayer& layer) {
    FractalParams unit = layer.fractal;
    unit.amplitude = 1.0f;
    if (layer.warpAmplitude > 0.0f) {
        float dx = 0.0f;
        float dy = 0.0f;
        DomainWarp2(x, y, layer, dx, dy);
        return Fractal2(x + dx, y + dy, unit);
    }
    return Fractal2(x, y, unit);
}

/// Height at world position (`x`, `y`) of `count` stacked layers, in world units (metres).
///
/// The first enabled layer sets the height (`amplitude * value`); each later enabled layer is
/// combined with it by its `blend` mode. Returns 0 if no layer is enabled.
inline float EvaluateNoiseLayers(float x, float y, const NoiseLayer* layers, std::size_t count) {
    float height = 0.0f;
    bool first = true;
    for (std::size_t i = 0; i < count; ++i) {
        const NoiseLayer& layer = layers[i];
        if (!layer.enabled) {
            continue;
        }
        const float value = EvaluateNoiseLayer(x, y, layer);
        const float scaled = layer.fractal.amplitude * value;
        if (first) {
            height = scaled;
            first = false;
            continue;
        }
        switch (layer.blend) {
        case LayerBlend::Max:
            height = std::max(height, scaled);
            break;
        case LayerBlend::Min:
            height = std::min(height, scaled);
            break;
        case LayerBlend::Mask:
            height *= 0.5f * (value + 1.0f);
            break;
        default:
            height += scaled;
            break;
        }
    }
    return height;
}

}  // namespace terraingen
