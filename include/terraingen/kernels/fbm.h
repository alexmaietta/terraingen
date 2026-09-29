#pragma once

#include "terraingen/kernels/hash.h"
#include "terraingen/kernels/perlin.h"

#include <cmath>
#include <cstdint>

namespace terraingen {

/// Parameters of fractional Brownian motion (fBm) built from Perlin noise octaves.
///
/// Octave i has wavelength `wavelength / lacunarity^i` and weight `gain^i`. Choosing
/// `gain = lacunarity^(−H)` gives a surface with Hurst exponent H (see `GainFromHurst`); H near 1
/// is smooth, lower H is rougher at short baselines. Lunar topography is self-affine over limited
/// baselines with baseline-dependent Hurst exponents (Rosenburg et al. 2011), which is what makes H
/// the natural roughness control here.
///
/// References:
/// - B. B. Mandelbrot and J. W. Van Ness, "Fractional Brownian Motions, Fractional Noises and
///   Applications", SIAM Review 10(4):422–437, 1968. https://doi.org/10.1137/1010093
/// - F. K. Musgrave, C. E. Kolb and R. S. Mace, "The Synthesis and Rendering of Eroded Fractal
///   Terrains", SIGGRAPH '89, Computer Graphics 23(3):41–50, 1989.
///   https://doi.org/10.1145/74334.74337
/// - F. K. Musgrave, "Procedural Fractal Terrains", in D. S. Ebert et al., Texturing and Modeling:
///   A Procedural Approach, 3rd ed., Morgan Kaufmann, 2003.
/// - M. A. Rosenburg et al., "Global Surface Slopes and Roughness of the Moon from the Lunar
///   Orbiter Laser Altimeter", Journal of Geophysical Research 116, E02001, 2011.
///   https://doi.org/10.1029/2010JE003716
struct FbmParams {
    std::uint32_t seed = 0;      ///< Seed; each octave derives its own seed and lattice offset.
    float wavelength = 1000.0f;  ///< Wavelength of the first octave, in world units (metres).
    float amplitude = 100.0f;    ///< Maximum absolute height, in world units (metres).
    int octaves = 8;             ///< Number of octaves; at least 1.
    float lacunarity = 2.0f;     ///< Frequency ratio between successive octaves; greater than 1.
    float gain = 0.5f;           ///< Weight ratio between successive octaves; greater than 0.
};

/// Octave gain that gives Hurst exponent `hurst` for the given lacunarity: `lacunarity^(−hurst)`.
///
/// Evaluated on the host; kernels only receive the resulting gain.
inline float GainFromHurst(float hurst, float lacunarity) {
    return std::pow(lacunarity, -hurst);
}

/// fBm height at world position (`x`, `y`), in world units.
///
/// Sums `params.octaves` octaves of `PerlinNoise2` and normalizes by the total octave weight, so
/// the result lies in [−amplitude, amplitude]. Each octave uses its own seed and a pseudo-random
/// sub-cell lattice offset from `pcg3d`; without the offset every octave's lattice would share the
/// origin, where Perlin noise is always zero, leaving a visible fixed point.
///
/// Uses only additions, multiplications and two divisions, so it ports directly to GPU kernels.
/// Parameters are assumed valid; see `ValidateFbmParams`.
inline float Fbm2(float x, float y, const FbmParams& params) {
    float frequency = 1.0f / params.wavelength;
    float weight = 1.0f;
    float sum = 0.0f;
    float totalWeight = 0.0f;
    for (int octave = 0; octave < params.octaves; ++octave) {
        const UVec3 h = Pcg3dHash(UVec3{params.seed, static_cast<std::uint32_t>(octave), 0u});
        const float offsetX = HashToUnitFloat(h.y);
        const float offsetY = HashToUnitFloat(h.z);
        sum += weight * PerlinNoise2(x * frequency + offsetX, y * frequency + offsetY, h.x);
        totalWeight += weight;
        weight *= params.gain;
        frequency *= params.lacunarity;
    }
    return params.amplitude * sum / totalWeight;
}

}  // namespace terraingen
