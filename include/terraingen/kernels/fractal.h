#pragma once

#include "terraingen/kernels/hash.h"
#include "terraingen/kernels/perlin.h"
#include "terraingen/kernels/simplex.h"
#include "terraingen/kernels/value.h"
#include "terraingen/kernels/worley.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace terraingen {

/// Single-octave noise function a fractal is built from.
enum class NoiseBasis : std::uint32_t {
    Perlin = 0,           ///< Improved gradient noise (`PerlinNoise2`).
    Simplex = 1,          ///< Simplex noise (`SimplexNoise2`).
    Value = 2,            ///< Value noise (`ValueNoise2`).
    WorleyF1 = 3,         ///< Distance to the nearest feature point; cones and basins.
    WorleyF2 = 4,         ///< Distance to the second-nearest feature point.
    WorleyF2MinusF1 = 5,  ///< F2 − F1; ridges along cell borders, like cracked ground.
};

inline constexpr std::uint32_t kNoiseBasisCount = 6;

/// How octaves of the basis noise are combined.
enum class FractalType : std::uint32_t {
    Fbm = 0,                 ///< Weighted sum of octaves (fractional Brownian motion).
    Ridged = 1,              ///< Ridged multifractal: sharp crests from inverted |noise|.
    Billow = 2,              ///< Sum of |noise| octaves: rounded, puffy forms.
    HybridMultifractal = 3,  ///< Smooth lowlands and rough highlands.
};

inline constexpr std::uint32_t kFractalTypeCount = 4;

/// Parameters of a fractal built from octaves of a basis noise.
///
/// Octave i has wavelength `wavelength / lacunarity^i` and spectral weight `gain^i`. Choosing
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
///   A Procedural Approach, 3rd ed., Morgan Kaufmann, 2003 (ridged and hybrid multifractals).
/// - M. A. Rosenburg et al., "Global Surface Slopes and Roughness of the Moon from the Lunar
///   Orbiter Laser Altimeter", Journal of Geophysical Research 116, E02001, 2011.
///   https://doi.org/10.1029/2010JE003716
struct FractalParams {
    std::uint32_t seed = 0;      ///< Seed; each octave derives its own seed and lattice offset.
    float wavelength = 1000.0f;  ///< Wavelength of the first octave, in world units (metres).
    float amplitude = 100.0f;    ///< Maximum absolute height, in world units (metres).
    int octaves = 8;             ///< Number of octaves; at least 1.
    float lacunarity = 2.0f;     ///< Frequency ratio between successive octaves; greater than 1.
    float gain = 0.5f;           ///< Weight ratio between successive octaves; greater than 0.
    NoiseBasis basis = NoiseBasis::Perlin;
    FractalType type = FractalType::Fbm;
    float offset = 1.0f;  ///< Ridge offset for `Ridged` and `HybridMultifractal`; > 0.

    bool operator==(const FractalParams&) const = default;
};

/// Octave gain that gives Hurst exponent `hurst` for the given lacunarity: `lacunarity^(−hurst)`.
///
/// Evaluated on the host; kernels only receive the resulting gain.
inline float GainFromHurst(float hurst, float lacunarity) {
    return std::pow(lacunarity, -hurst);
}

/// One octave of `basis` noise at (`x`, `y`) in lattice units, in [−1, 1].
///
/// Worley distances are mapped linearly from [0, bound] to [−1, 1], where the bound is the largest
/// possible distance with one feature point per cell: √2 for F1 and √5 for F2 and F2 − F1.
inline float BasisNoise2(NoiseBasis basis, float x, float y, std::uint32_t seed) {
    constexpr float kInvSqrt2 = 0.707106781186547524f;
    constexpr float kInvSqrt5 = 0.447213595499957939f;
    switch (basis) {
    case NoiseBasis::Simplex:
        return SimplexNoise2(x, y, seed);
    case NoiseBasis::Value:
        return ValueNoise2(x, y, seed);
    case NoiseBasis::WorleyF1:
        return 2.0f * std::min(Worley2(x, y, seed).f1 * kInvSqrt2, 1.0f) - 1.0f;
    case NoiseBasis::WorleyF2:
        return 2.0f * std::min(Worley2(x, y, seed).f2 * kInvSqrt5, 1.0f) - 1.0f;
    case NoiseBasis::WorleyF2MinusF1: {
        const WorleyDistances d = Worley2(x, y, seed);
        return 2.0f * std::min((d.f2 - d.f1) * kInvSqrt5, 1.0f) - 1.0f;
    }
    default:
        return PerlinNoise2(x, y, seed);
    }
}

/// Fractal height at world position (`x`, `y`), in [−amplitude, amplitude] world units.
///
/// Each octave uses its own seed and a pseudo-random sub-cell lattice offset from `pcg3d`; without
/// the offset every octave's lattice would share the origin, leaving a visible fixed point. Every
/// fractal type is normalized by its largest possible value so the bound holds for any parameters:
/// - `Fbm`: Σ wᵢ nᵢ / Σ wᵢ.
/// - `Billow`: fBm of 2|nᵢ| − 1.
/// - `Ridged`: Musgrave's ridged multifractal, sᵢ = (offset − |nᵢ|)² weighted by the previous
///   octave's signal (feedback gain 2, clamped to [0, 1]), rescaled from [0, 1] to [−1, 1].
/// - `HybridMultifractal`: Musgrave's hybrid multifractal with (nᵢ + offset) signals and the
/// running
///   weight clamped to [0, 1], divided by (1 + offset) Σ wᵢ.
///
/// Uses only arithmetic, comparisons and `floor`, so it ports directly to GPU kernels. Parameters
/// are assumed valid; see `ValidateFractalParams`.
inline float Fractal2(float x, float y, const FractalParams& params) {
    float frequency = 1.0f / params.wavelength;
    float weight = 1.0f;
    float sum = 0.0f;
    float totalWeight = 0.0f;
    float feedback = 1.0f;
    const float ridgeScale = std::max(params.offset, 1.0f - params.offset);
    for (int octave = 0; octave < params.octaves; ++octave) {
        const UVec3 h = Pcg3dHash(UVec3{params.seed, static_cast<std::uint32_t>(octave), 0u});
        const float offsetX = HashToUnitFloat(h.y);
        const float offsetY = HashToUnitFloat(h.z);
        const float n =
            BasisNoise2(params.basis, x * frequency + offsetX, y * frequency + offsetY, h.x);
        switch (params.type) {
        case FractalType::Billow:
            sum += weight * (2.0f * std::fabs(n) - 1.0f);
            break;
        case FractalType::Ridged: {
            float signal = params.offset - std::fabs(n);
            signal *= signal * feedback;
            feedback = std::clamp(2.0f * signal, 0.0f, 1.0f);
            sum += weight * signal;
            break;
        }
        case FractalType::HybridMultifractal: {
            const float signal = (n + params.offset) * weight;
            if (octave == 0) {
                sum = signal;
                feedback = signal;
            } else {
                feedback = std::clamp(feedback, 0.0f, 1.0f);
                sum += feedback * signal;
                feedback *= signal;
            }
            break;
        }
        default:
            sum += weight * n;
            break;
        }
        totalWeight += weight;
        weight *= params.gain;
        frequency *= params.lacunarity;
    }

    switch (params.type) {
    case FractalType::Ridged:
        return params.amplitude * (2.0f * sum / (ridgeScale * ridgeScale * totalWeight) - 1.0f);
    case FractalType::HybridMultifractal:
        return params.amplitude * sum / ((1.0f + params.offset) * totalWeight);
    default:
        return params.amplitude * sum / totalWeight;
    }
}

}  // namespace terraingen
