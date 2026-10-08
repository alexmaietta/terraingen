#pragma once

#include "terraingen/noise/kernels/fractal.h"

namespace terraingen {

/// How a noise layer combines with the height accumulated from the layers before it.
enum class LayerBlend : uint32_t {
    Add = 0,   ///< height + layer
    Max = 1,   ///< max(height, layer)
    Min = 2,   ///< min(height, layer)
    Mask = 3,  ///< height * (layer / amplitude + 1) / 2: the layer modulates roughness by region.
};

static const uint32_t kLayerBlendCount = 4;

/// Most layers a synthetic terrain may stack; a fixed bound keeps layers uploadable to GPUs.
static const uint32_t kMaxNoiseLayers = 8;

/// One layer of a synthetic terrain: a fractal, an optional domain warp and a blend mode.
struct NoiseLayer {
    bool enabled = true;
    FractalParams fractal;
    LayerBlend blend = LayerBlend::Add;
    float warpAmplitude = 0.0f;     ///< Domain warp displacement in metres; 0 disables warping.
    float warpWavelength = 500.0f;  ///< Wavelength of the warp field in metres.

#ifndef __SLANG__
    bool operator==(const NoiseLayer&) const = default;
#endif
};

/// Up to `kMaxNoiseLayers` layers in a fixed-size block, the form GPU kernels receive them in.
struct NoiseLayers {
    NoiseLayer layers[kMaxNoiseLayers];
    uint32_t count = 0;
};

/// A domain warp offset in metres.
struct WarpOffset2 {
    float dx = 0.0f;
    float dy = 0.0f;
};

/// Domain warp offset at (`x`, `y`): two independent 4-octave Perlin fBm fields of
/// `layer.warpWavelength`, each bounded by `layer.warpAmplitude` metres.
///
/// Reference: I. Quilez, "Domain Warping", 2002. https://iquilezles.org/articles/warp/
inline WarpOffset2 DomainWarp2(float x, float y, NoiseLayer layer) {
    FractalParams warp = {};
    warp.wavelength = layer.warpWavelength;
    warp.amplitude = layer.warpAmplitude;
    warp.octaves = 4;
    warp.seed = PcgHash(layer.fractal.seed ^ 0x5bd1e995u);
    WarpOffset2 offset = {};
    offset.dx = Fractal2(x, y, warp);
    warp.seed = PcgHash(warp.seed);
    offset.dy = Fractal2(x, y, warp);
    return offset;
}

/// Normalized value of `layer` at (`x`, `y`), in [-1, 1], before amplitude and blending.
inline float EvaluateNoiseLayer(float x, float y, NoiseLayer layer) {
    FractalParams unit = layer.fractal;
    unit.amplitude = 1.0f;
    if (layer.warpAmplitude > 0.0f) {
        const WarpOffset2 offset = DomainWarp2(x, y, layer);
        return Fractal2(x + offset.dx, y + offset.dy, unit);
    }
    return Fractal2(x, y, unit);
}

namespace detail {

/// The height of a stack of layers so far, and whether any layer has set it yet.
struct NoiseStack {
    float height = 0.0f;
    bool started = false;
};

/// `stack` combined with an enabled `layer` whose normalized value is `value`.
inline NoiseStack BlendNoiseLayer(NoiseStack stack, NoiseLayer layer, float value) {
    const float scaled = layer.fractal.amplitude * value;
    if (!stack.started) {
        stack.height = scaled;
        stack.started = true;
        return stack;
    }
    switch (layer.blend) {
    case LayerBlend::Max:
        stack.height = max(stack.height, scaled);
        break;
    case LayerBlend::Min:
        stack.height = min(stack.height, scaled);
        break;
    case LayerBlend::Mask:
        stack.height *= 0.5f * (value + 1.0f);
        break;
    default:
        stack.height += scaled;
        break;
    }
    return stack;
}

/// `stack` with `layer` at (`x`, `y`) added, if it is enabled.
inline NoiseStack AddNoiseLayer2(NoiseStack stack, float x, float y, NoiseLayer layer) {
    if (!layer.enabled) {
        return stack;
    }
    return BlendNoiseLayer(stack, layer, EvaluateNoiseLayer(x, y, layer));
}

}  // namespace detail

/// Height at world position (`x`, `y`) of the stacked `layers`, in world units (metres).
///
/// The first enabled layer sets the height (`amplitude * value`); each later enabled layer is
/// combined with it by its `blend` mode. Returns 0 if no layer is enabled.
inline float EvaluateNoiseLayers(float x, float y, NoiseLayers layers) {
    detail::NoiseStack stack = {};
    for (uint32_t i = 0; i < layers.count; ++i) {
        stack = detail::AddNoiseLayer2(stack, x, y, layers.layers[i]);
    }
    return stack.height;
}

#ifndef __SLANG__
/// `EvaluateNoiseLayers` of the `count` layers at `layers`.
inline float EvaluateNoiseLayers(float x, float y, const NoiseLayer* layers, std::size_t count) {
    detail::NoiseStack stack;
    for (std::size_t i = 0; i < count; ++i) {
        stack = detail::AddNoiseLayer2(stack, x, y, layers[i]);
    }
    return stack.height;
}

/// `EvaluateNoiseLayers` in 3D at world position (`x`, `y`, `z`), with `Fractal3` and a 3D domain
/// warp (three fields like `DomainWarp2`'s), for noise on a sphere.
inline float EvaluateNoiseLayers3(double x, double y, double z, const NoiseLayer* layers,
                                  std::size_t count) {
    detail::NoiseStack stack;
    for (std::size_t i = 0; i < count; ++i) {
        const NoiseLayer& layer = layers[i];
        if (!layer.enabled) {
            continue;
        }
        FractalParams unit = layer.fractal;
        unit.amplitude = 1.0f;
        float value = 0.0f;
        if (layer.warpAmplitude > 0.0f) {
            FractalParams warp;
            warp.wavelength = layer.warpWavelength;
            warp.amplitude = layer.warpAmplitude;
            warp.octaves = 4;
            warp.seed = PcgHash(layer.fractal.seed ^ 0x5bd1e995u);
            const double dx = static_cast<double>(Fractal3(x, y, z, warp));
            warp.seed = PcgHash(warp.seed);
            const double dy = static_cast<double>(Fractal3(x, y, z, warp));
            warp.seed = PcgHash(warp.seed);
            const double dz = static_cast<double>(Fractal3(x, y, z, warp));
            value = Fractal3(x + dx, y + dy, z + dz, unit);
        } else {
            value = Fractal3(x, y, z, unit);
        }
        stack = detail::BlendNoiseLayer(stack, layer, value);
    }
    return stack.height;
}
#endif

}  // namespace terraingen
