// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Stacks of fractals with domain warps and blend modes.
#pragma once

#include "terraingen/noise/fractal.h"

namespace terraingen {

/// How a fractal combines with the height accumulated from the fractals before it.
enum class FractalBlend : uint32_t {
    Add = 0,  ///< height + fractal
    Max = 1,  ///< max(height, fractal)
    Min = 2,  ///< min(height, fractal)
    Mask =
        3,  ///< height * (fractal / amplitude + 1) / 2: the fractal modulates roughness by region.
};

/// Number of `FractalBlend` values.
static const uint32_t kFractalBlendCount = 4;

/// Most fractals a stack may hold; a fixed bound keeps stacks uploadable to GPUs.
static const uint32_t kMaxFractals = 8;

/// One fractal of a stack: the `FractalParams` fields, an optional domain warp and a blend mode.
/// The fields keep the std430 layout GPU kernels read: `enabled`, then the `FractalParams` fields
/// in their order, then the blend and warp.
struct Fractal {
    bool enabled = true;         ///< Whether the fractal contributes.
    uint32_t seed = 0;           ///< Seed; each octave derives its own seed and lattice offset.
    float wavelength = 1000.0f;  ///< Wavelength of the first octave, in world units (meters).
    float amplitude = 100.0f;    ///< Maximum absolute height, in world units (meters).
    int octaves = 8;             ///< Number of octaves; at least 1.
    float lacunarity = 2.0f;     ///< Frequency ratio between successive octaves; greater than 1.
    float gain = 0.5f;           ///< Weight ratio between successive octaves; greater than 0.
    NoiseBasis basis = NoiseBasis::Perlin;  ///< Single-octave noise.
    FractalType type = FractalType::Fbm;    ///< How octaves combine.
    float offset = 1.0f;  ///< Ridge offset for `Ridged` and `HybridMultifractal`; > 0.
    FractalBlend blend = FractalBlend::Add;  ///< How it combines with the fractals before it.
    float warpAmplitude = 0.0f;     ///< Domain warp displacement in meters; 0 disables warping.
    float warpWavelength = 500.0f;  ///< Wavelength of the warp field in meters.

#ifndef __SLANG__
    /// An fBm fractal of `seed` whose broadest octave is `wavelength` meters across and whose
    /// height reaches `amplitude` meters, the rest at its defaults: `Fractal::Fbm(1, 800.0, 40.0)`.
    static Fractal Fbm(uint32_t seed, double wavelength, double amplitude) {
        Fractal fractal;
        fractal.seed = seed;
        fractal.wavelength = static_cast<float>(wavelength);
        fractal.amplitude = static_cast<float>(amplitude);
        return fractal;
    }

    /// Memberwise equality.
    bool operator==(const Fractal& other) const {
        return enabled == other.enabled && seed == other.seed && wavelength == other.wavelength &&
               amplitude == other.amplitude && octaves == other.octaves &&
               lacunarity == other.lacunarity && gain == other.gain && basis == other.basis &&
               type == other.type && offset == other.offset && blend == other.blend &&
               warpAmplitude == other.warpAmplitude && warpWavelength == other.warpWavelength;
    }
    bool operator!=(const Fractal& other) const {
        return !(*this == other);
    }
#endif
};

/// The `FractalParams` part of `fractal`, which `Fractal2` and `Fractal3` evaluate.
inline FractalParams FractalParamsFrom(Fractal fractal) {
    FractalParams params = {};
    params.seed = fractal.seed;
    params.wavelength = fractal.wavelength;
    params.amplitude = fractal.amplitude;
    params.octaves = fractal.octaves;
    params.lacunarity = fractal.lacunarity;
    params.gain = fractal.gain;
    params.basis = fractal.basis;
    params.type = fractal.type;
    params.offset = fractal.offset;
    return params;
}

/// Up to `kMaxFractals` fractals in a fixed-size block, the form GPU kernels receive.
struct FractalStack {
    Fractal fractals[kMaxFractals];  ///< The fractals; only the first `count` are used.
    uint32_t count = 0;              ///< Number of fractals used, at most `kMaxFractals`.
};

/// A 2D domain warp offset.
struct WarpOffset2 {
    float dx = 0.0f;  ///< Offset in x, in meters.
    float dy = 0.0f;  ///< Offset in y, in meters.
};

namespace detail {

/// Mixed into a fractal's seed to seed its warp fields apart from its own noise. Frozen for
/// reproducibility.
static const uint32_t kWarpSeedSalt = 0x5bd1e995u;

}  // namespace detail

/// The fractal of the first warp field of `layer`: 4-octave Perlin fBm of the fractal's warp
/// wavelength and amplitude. Each further field is seeded with `PcgHash` of the one before.
inline FractalParams WarpParams(Fractal layer) {
    FractalParams warp = {};
    warp.wavelength = layer.warpWavelength;
    warp.amplitude = layer.warpAmplitude;
    warp.octaves = 4;
    warp.seed = PcgHash(layer.seed ^ detail::kWarpSeedSalt);
    return warp;
}

/// A fractal's domain warp offset from two independent warp fields (`WarpParams`).
/// @param x, y World coordinates in meters.
/// @return The offset, each component bounded by `layer.warpAmplitude` meters.
inline WarpOffset2 DomainWarp2(float x, float y, Fractal layer) {
    FractalParams warp = WarpParams(layer);
    WarpOffset2 offset = {};
    offset.dx = Fractal2(x, y, warp);
    warp.seed = PcgHash(warp.seed);
    offset.dy = Fractal2(x, y, warp);
    return offset;
}

/// A fractal's normalized value, warped when `warpAmplitude` > 0, before amplitude and
/// blending.
/// @param x, y World coordinates in meters.
/// @return The value in [-1, 1].
inline float EvaluateFractal(float x, float y, Fractal layer) {
    FractalParams unit = FractalParamsFrom(layer);
    unit.amplitude = 1.0f;
    if (layer.warpAmplitude > 0.0f) {
        const WarpOffset2 offset = DomainWarp2(x, y, layer);
        return Fractal2(x + offset.dx, y + offset.dy, unit);
    }
    return Fractal2(x, y, unit);
}

namespace detail {

/// The height of a stack of fractals so far.
struct NoiseStack {
    float height = 0.0f;   ///< Height so far, in meters.
    bool started = false;  ///< Whether any fractal has set the height yet.
};

/// Combines an enabled fractal with a stack; the first fractal sets the height.
/// @param value The fractal's normalized value, in [-1, 1].
inline NoiseStack BlendFractal(NoiseStack stack, Fractal layer, float value) {
    const float scaled = layer.amplitude * value;
    if (!stack.started) {
        stack.height = scaled;
        stack.started = true;
        return stack;
    }
    switch (layer.blend) {
    case FractalBlend::Max:
        stack.height = max(stack.height, scaled);
        break;
    case FractalBlend::Min:
        stack.height = min(stack.height, scaled);
        break;
    case FractalBlend::Mask:
        stack.height *= 0.5f * (value + 1.0f);
        break;
    default:
        stack.height += scaled;
        break;
    }
    return stack;
}

/// Adds a fractal at a 2D point to a stack, if the fractal is enabled.
/// @param x, y World coordinates in meters.
inline NoiseStack AddFractal2(NoiseStack stack, float x, float y, Fractal layer) {
    if (!layer.enabled) {
        return stack;
    }
    return BlendFractal(stack, layer, EvaluateFractal(x, y, layer));
}

}  // namespace detail

/// Height of stacked fractals: the first enabled fractal sets it (`amplitude * value`), each
/// later enabled fractal combines by its `blend` mode.
/// @param x, y World coordinates in meters.
/// @return Height in meters; 0 if no fractal is enabled.
inline float EvaluateFractals(float x, float y, FractalStack stack) {
    detail::NoiseStack height = {};
    for (uint32_t i = 0; i < stack.count; ++i) {
        height = detail::AddFractal2(height, x, y, stack.fractals[i]);
    }
    return height.height;
}

#ifndef __SLANG__
/// `EvaluateFractals` of any number of fractals, including more than a GPU block holds.
/// @param x, y World coordinates in meters.
/// @param fractals The first of `count` fractals.
/// @return Height in meters; 0 if no fractal is enabled.
inline float EvaluateFractals(float x, float y, const Fractal* fractals, std::size_t count) {
    detail::NoiseStack stack;
    for (std::size_t i = 0; i < count; ++i) {
        stack = detail::AddFractal2(stack, x, y, fractals[i]);
    }
    return stack.height;
}

/// A 3D domain warp offset.
struct WarpOffset3 {
    double dx = 0.0;  ///< Offset in x, in meters.
    double dy = 0.0;  ///< Offset in y, in meters.
    double dz = 0.0;  ///< Offset in z, in meters.
};

/// `DomainWarp2` in 3D: three warp fields (`WarpParams`) evaluated with `Fractal3`.
/// @param x, y, z World coordinates in meters.
/// @return The offset, each component bounded by `layer.warpAmplitude` meters.
inline WarpOffset3 DomainWarp3(double x, double y, double z, Fractal layer) {
    FractalParams warp = WarpParams(layer);
    WarpOffset3 offset;
    offset.dx = static_cast<double>(Fractal3(x, y, z, warp));
    warp.seed = PcgHash(warp.seed);
    offset.dy = static_cast<double>(Fractal3(x, y, z, warp));
    warp.seed = PcgHash(warp.seed);
    offset.dz = static_cast<double>(Fractal3(x, y, z, warp));
    return offset;
}

/// `EvaluateFractal` in 3D, with `Fractal3` and `DomainWarp3`.
/// @param x, y, z World coordinates in meters.
/// @return The fractal's normalized value in [-1, 1].
inline float EvaluateFractal3(double x, double y, double z, Fractal layer) {
    FractalParams unit = FractalParamsFrom(layer);
    unit.amplitude = 1.0f;
    if (layer.warpAmplitude > 0.0f) {
        const WarpOffset3 offset = DomainWarp3(x, y, z, layer);
        return Fractal3(x + offset.dx, y + offset.dy, z + offset.dz, unit);
    }
    return Fractal3(x, y, z, unit);
}

namespace detail {

/// Adds a fractal at a 3D point to a stack, if the fractal is enabled.
/// @param x, y, z World coordinates in meters.
inline NoiseStack AddFractal3(NoiseStack stack, double x, double y, double z, Fractal layer) {
    if (!layer.enabled) {
        return stack;
    }
    return BlendFractal(stack, layer, EvaluateFractal3(x, y, z, layer));
}

}  // namespace detail

/// `EvaluateFractals` in 3D with `EvaluateFractal3`, for noise on a sphere.
/// @param x, y, z World coordinates in meters.
/// @param fractals The first of `count` fractals.
/// @return Height in meters; 0 if no fractal is enabled.
inline float EvaluateFractals3(double x, double y, double z, const Fractal* fractals,
                               std::size_t count) {
    detail::NoiseStack stack;
    for (std::size_t i = 0; i < count; ++i) {
        stack = detail::AddFractal3(stack, x, y, z, fractals[i]);
    }
    return stack.height;
}
#endif

}  // namespace terraingen
