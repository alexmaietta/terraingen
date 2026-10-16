/// @file
/// @brief Stacks of fractal noise layers with domain warps and blend modes.
#pragma once

#include "terraingen/noise/kernels/fractal.h"

namespace terraingen {

/// @brief How a noise layer combines with the height accumulated from the layers before it.
enum class LayerBlend : uint32_t {
    Add = 0,   ///< height + layer
    Max = 1,   ///< max(height, layer)
    Min = 2,   ///< min(height, layer)
    Mask = 3,  ///< height * (layer / amplitude + 1) / 2: the layer modulates roughness by region.
};

/// @brief Number of `LayerBlend` values.
static const uint32_t kLayerBlendCount = 4;

/// @brief Most layers a synthetic terrain may stack; a fixed bound keeps layers uploadable to GPUs.
static const uint32_t kMaxNoiseLayers = 8;

/// @brief One layer of a synthetic terrain: a fractal, an optional domain warp and a blend mode.
struct NoiseLayer {
    bool enabled = true;                 ///< Whether the layer contributes.
    FractalParams fractal;               ///< The layer's fractal; its amplitude scales the layer.
    LayerBlend blend = LayerBlend::Add;  ///< How it combines with the layers before it.
    float warpAmplitude = 0.0f;     ///< Domain warp displacement in metres; 0 disables warping.
    float warpWavelength = 500.0f;  ///< Wavelength of the warp field in metres.

#ifndef __SLANG__
    bool operator==(const NoiseLayer&) const = default;
#endif
};

/// @brief Up to `kMaxNoiseLayers` layers in a fixed-size block, the form GPU kernels receive.
struct NoiseLayers {
    NoiseLayer layers[kMaxNoiseLayers];  ///< The layers; only the first `count` are used.
    uint32_t count = 0;                  ///< Number of layers used, at most `kMaxNoiseLayers`.
};

/// @brief A 2D domain warp offset.
struct WarpOffset2 {
    float dx = 0.0f;  ///< Offset in x, in metres.
    float dy = 0.0f;  ///< Offset in y, in metres.
};

namespace detail {

/// @brief Mixed into a layer's seed to seed its warp fields apart from its own noise. Frozen for
/// reproducibility.
static const uint32_t kWarpSeedSalt = 0x5bd1e995u;

}  // namespace detail

/// @brief The fractal of the first warp field of `layer`: 4-octave Perlin fBm of the layer's warp
/// wavelength and amplitude. Each further field is seeded with `PcgHash` of the one before.
/// @param layer The layer.
/// @return The warp field's parameters.
inline FractalParams WarpParams(NoiseLayer layer) {
    FractalParams warp = {};
    warp.wavelength = layer.warpWavelength;
    warp.amplitude = layer.warpAmplitude;
    warp.octaves = 4;
    warp.seed = PcgHash(layer.fractal.seed ^ detail::kWarpSeedSalt);
    return warp;
}

/// @brief A layer's domain warp offset from two independent warp fields (`WarpParams`).
/// @param x World x in metres.
/// @param y World y in metres.
/// @param layer The layer whose warp is evaluated.
/// @return The offset, each component bounded by `layer.warpAmplitude` metres.
inline WarpOffset2 DomainWarp2(float x, float y, NoiseLayer layer) {
    FractalParams warp = WarpParams(layer);
    WarpOffset2 offset = {};
    offset.dx = Fractal2(x, y, warp);
    warp.seed = PcgHash(warp.seed);
    offset.dy = Fractal2(x, y, warp);
    return offset;
}

/// @brief A layer's normalized value, warped when `warpAmplitude` > 0, before amplitude and
/// blending.
/// @param x World x in metres.
/// @param y World y in metres.
/// @param layer The layer.
/// @return The value in [-1, 1].
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

/// @brief The height of a stack of layers so far.
struct NoiseStack {
    float height = 0.0f;   ///< Height so far, in metres.
    bool started = false;  ///< Whether any layer has set the height yet.
};

/// @brief Combines an enabled layer with a stack; the first layer sets the height.
/// @param stack The stack before the layer.
/// @param layer The layer.
/// @param value The layer's normalized value, in [-1, 1].
/// @return The stack after the layer.
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

/// @brief Adds a layer at a 2D point to a stack, if the layer is enabled.
/// @param stack The stack before the layer.
/// @param x World x in metres.
/// @param y World y in metres.
/// @param layer The layer.
/// @return The stack after the layer.
inline NoiseStack AddNoiseLayer2(NoiseStack stack, float x, float y, NoiseLayer layer) {
    if (!layer.enabled) {
        return stack;
    }
    return BlendNoiseLayer(stack, layer, EvaluateNoiseLayer(x, y, layer));
}

}  // namespace detail

/// @brief Height of stacked layers: the first enabled layer sets it (`amplitude * value`), each
/// later enabled layer combines by its `blend` mode.
/// @param x World x in metres.
/// @param y World y in metres.
/// @param layers The layers.
/// @return Height in metres; 0 if no layer is enabled.
inline float EvaluateNoiseLayers(float x, float y, NoiseLayers layers) {
    detail::NoiseStack stack = {};
    for (uint32_t i = 0; i < layers.count; ++i) {
        stack = detail::AddNoiseLayer2(stack, x, y, layers.layers[i]);
    }
    return stack.height;
}

#ifndef __SLANG__
/// @brief `EvaluateNoiseLayers` of any number of layers, including more than a GPU block holds.
/// @param x World x in metres.
/// @param y World y in metres.
/// @param layers The first of `count` layers.
/// @param count Number of layers.
/// @return Height in metres; 0 if no layer is enabled.
inline float EvaluateNoiseLayers(float x, float y, const NoiseLayer* layers, std::size_t count) {
    detail::NoiseStack stack;
    for (std::size_t i = 0; i < count; ++i) {
        stack = detail::AddNoiseLayer2(stack, x, y, layers[i]);
    }
    return stack.height;
}

/// @brief A 3D domain warp offset.
struct WarpOffset3 {
    double dx = 0.0;  ///< Offset in x, in metres.
    double dy = 0.0;  ///< Offset in y, in metres.
    double dz = 0.0;  ///< Offset in z, in metres.
};

/// @brief `DomainWarp2` in 3D: three warp fields (`WarpParams`) evaluated with `Fractal3`.
/// @param x World x in metres.
/// @param y World y in metres.
/// @param z World z in metres.
/// @param layer The layer whose warp is evaluated.
/// @return The offset, each component bounded by `layer.warpAmplitude` metres.
inline WarpOffset3 DomainWarp3(double x, double y, double z, NoiseLayer layer) {
    FractalParams warp = WarpParams(layer);
    WarpOffset3 offset;
    offset.dx = static_cast<double>(Fractal3(x, y, z, warp));
    warp.seed = PcgHash(warp.seed);
    offset.dy = static_cast<double>(Fractal3(x, y, z, warp));
    warp.seed = PcgHash(warp.seed);
    offset.dz = static_cast<double>(Fractal3(x, y, z, warp));
    return offset;
}

/// @brief `EvaluateNoiseLayer` in 3D, with `Fractal3` and `DomainWarp3`.
/// @param x World x in metres.
/// @param y World y in metres.
/// @param z World z in metres.
/// @param layer The layer.
/// @return The layer's normalized value in [-1, 1].
inline float EvaluateNoiseLayer3(double x, double y, double z, NoiseLayer layer) {
    FractalParams unit = layer.fractal;
    unit.amplitude = 1.0f;
    if (layer.warpAmplitude > 0.0f) {
        const WarpOffset3 offset = DomainWarp3(x, y, z, layer);
        return Fractal3(x + offset.dx, y + offset.dy, z + offset.dz, unit);
    }
    return Fractal3(x, y, z, unit);
}

namespace detail {

/// @brief Adds a layer at a 3D point to a stack, if the layer is enabled.
/// @param stack The stack before the layer.
/// @param x World x in metres.
/// @param y World y in metres.
/// @param z World z in metres.
/// @param layer The layer.
/// @return The stack after the layer.
inline NoiseStack AddNoiseLayer3(NoiseStack stack, double x, double y, double z, NoiseLayer layer) {
    if (!layer.enabled) {
        return stack;
    }
    return BlendNoiseLayer(stack, layer, EvaluateNoiseLayer3(x, y, z, layer));
}

}  // namespace detail

/// @brief `EvaluateNoiseLayers` in 3D with `EvaluateNoiseLayer3`, for noise on a sphere.
/// @param x World x in metres.
/// @param y World y in metres.
/// @param z World z in metres.
/// @param layers The first of `count` layers.
/// @param count Number of layers.
/// @return Height in metres; 0 if no layer is enabled.
inline float EvaluateNoiseLayers3(double x, double y, double z, const NoiseLayer* layers,
                                  std::size_t count) {
    detail::NoiseStack stack;
    for (std::size_t i = 0; i < count; ++i) {
        stack = detail::AddNoiseLayer3(stack, x, y, z, layers[i]);
    }
    return stack.height;
}
#endif

}  // namespace terraingen
