#pragma once

#include "terraingen/noise/kernels/hash.h"
#include "terraingen/noise/kernels/perlin.h"
#include "terraingen/noise/kernels/simplex.h"
#include "terraingen/noise/kernels/value.h"
#include "terraingen/noise/kernels/worley.h"

namespace terraingen {

/// Single-octave noise function a fractal is built from.
enum class NoiseBasis : uint32_t {
    Perlin = 0,           ///< Improved gradient noise (`PerlinNoise2`).
    Simplex = 1,          ///< Simplex noise (`SimplexNoise2`).
    Value = 2,            ///< Value noise (`ValueNoise2`).
    WorleyF1 = 3,         ///< Distance to the nearest feature point; cones and basins.
    WorleyF2 = 4,         ///< Distance to the second-nearest feature point.
    WorleyF2MinusF1 = 5,  ///< F2 - F1; ridges along cell borders, like cracked ground.
};

static const uint32_t kNoiseBasisCount = 6;

/// How octaves of the basis noise are combined.
enum class FractalType : uint32_t {
    Fbm = 0,                 ///< Weighted sum of octaves (fractional Brownian motion).
    Ridged = 1,              ///< Ridged multifractal: sharp crests from inverted |noise|.
    Billow = 2,              ///< Sum of |noise| octaves: rounded, puffy forms.
    HybridMultifractal = 3,  ///< Smooth lowlands and rough highlands.
};

static const uint32_t kFractalTypeCount = 4;

/// Parameters of a fractal built from octaves of a basis noise.
///
/// Octave i has wavelength `wavelength / lacunarity^i` and spectral weight `gain^i`. Choosing
/// `gain = lacunarity^(-H)` gives a surface with Hurst exponent H (see `GainFromHurst`); H near 1
/// is smooth, lower H is rougher at short baselines. Lunar topography is self-affine over limited
/// baselines with baseline-dependent Hurst exponents (Rosenburg et al. 2011), which is what makes H
/// the natural roughness control here.
///
/// References:
/// - B. B. Mandelbrot and J. W. Van Ness, "Fractional Brownian Motions, Fractional Noises and
///   Applications", SIAM Review 10(4):422-437, 1968. https://doi.org/10.1137/1010093
/// - F. K. Musgrave, C. E. Kolb and R. S. Mace, "The Synthesis and Rendering of Eroded Fractal
///   Terrains", SIGGRAPH '89, Computer Graphics 23(3):41-50, 1989.
///   https://doi.org/10.1145/74334.74337
/// - F. K. Musgrave, "Procedural Fractal Terrains", in D. S. Ebert et al., Texturing and Modeling:
///   A Procedural Approach, 3rd ed., Morgan Kaufmann, 2003 (ridged and hybrid multifractals).
/// - M. A. Rosenburg et al., "Global Surface Slopes and Roughness of the Moon from the Lunar
///   Orbiter Laser Altimeter", Journal of Geophysical Research 116, E02001, 2011.
///   https://doi.org/10.1029/2010JE003716
struct FractalParams {
    uint32_t seed = 0;           ///< Seed; each octave derives its own seed and lattice offset.
    float wavelength = 1000.0f;  ///< Wavelength of the first octave, in world units (metres).
    float amplitude = 100.0f;    ///< Maximum absolute height, in world units (metres).
    int octaves = 8;             ///< Number of octaves; at least 1.
    float lacunarity = 2.0f;     ///< Frequency ratio between successive octaves; greater than 1.
    float gain = 0.5f;           ///< Weight ratio between successive octaves; greater than 0.
    NoiseBasis basis = NoiseBasis::Perlin;
    FractalType type = FractalType::Fbm;
    float offset = 1.0f;  ///< Ridge offset for `Ridged` and `HybridMultifractal`; > 0.

#ifndef __SLANG__
    bool operator==(const FractalParams&) const = default;
#endif
};

/// Octave gain that gives Hurst exponent `hurst` for the given lacunarity: `lacunarity^(-hurst)`.
///
/// Evaluated on the host; kernels only receive the resulting gain.
inline float GainFromHurst(float hurst, float lacunarity) {
    return pow(lacunarity, -hurst);
}

/// One octave of `basis` noise at (`x`, `y`) in lattice units, in [-1, 1].
///
/// Worley distances are mapped linearly from [0, bound] to [-1, 1], where the bound is the largest
/// possible distance with one feature point per cell: sqrt(2) for F1, and sqrt(5) for F2
/// and F2 - F1.
inline float BasisNoise2(NoiseBasis basis, float x, float y, uint32_t seed) {
    constexpr float kInvSqrt2 = 0.707106781186547524f;
    constexpr float kInvSqrt5 = 0.447213595499957939f;
    switch (basis) {
    case NoiseBasis::Simplex:
        return SimplexNoise2(x, y, seed);
    case NoiseBasis::Value:
        return ValueNoise2(x, y, seed);
    case NoiseBasis::WorleyF1:
        return 2.0f * min(Worley2(x, y, seed).f1 * kInvSqrt2, 1.0f) - 1.0f;
    case NoiseBasis::WorleyF2:
        return 2.0f * min(Worley2(x, y, seed).f2 * kInvSqrt5, 1.0f) - 1.0f;
    case NoiseBasis::WorleyF2MinusF1: {
        const WorleyDistances d = Worley2(x, y, seed);
        return 2.0f * min((d.f2 - d.f1) * kInvSqrt5, 1.0f) - 1.0f;
    }
    default:
        return PerlinNoise2(x, y, seed);
    }
}

/// Whether `basis` has a 3D form for `Fractal3`: Perlin and value noise.
inline bool HasBasisNoise3(NoiseBasis basis) {
    return basis == NoiseBasis::Perlin || basis == NoiseBasis::Value;
}

/// @brief One octave of 3D `basis` noise: value noise for `NoiseBasis::Value`, Perlin noise
/// otherwise.
/// @param basis The basis.
/// @param point The lattice cell and the fraction within it.
/// @param seed The noise seed.
/// @return Noise in [-1, 1].
inline float BasisNoise3(NoiseBasis basis, LatticePoint3 point, uint32_t seed) {
    if (basis == NoiseBasis::Value) {
        return ValueNoise3(point, seed);
    }
    return PerlinNoise3(point, seed);
}

namespace detail {

/// Each octave's lattice is turned by the golden angle (about 137.5 degrees) from the one before,
/// so the creases that axis-aligned gradients leave along lattice lines do not line up from octave
/// to octave into a grid. The turn is accumulated by multiplication rather than `cos` and `sin`,
/// whose last bits differ between platforms, so results stay bit-identical everywhere.
static const float kOctaveTurnCos = -0.7373688780783197f;
static const float kOctaveTurnSin = 0.6754902942615238f;

/// The running sums of a fractal over its octaves.
struct FractalSums {
    float weight = 1.0f;
    float sum = 0.0f;
    float totalWeight = 0.0f;
    float feedback = 1.0f;
};

/// `sums` after octave `octave`, whose basis noise is `n`.
inline FractalSums AddFractalOctave(FractalSums sums, FractalParams params, int octave, float n) {
    switch (params.type) {
    case FractalType::Billow:
        sums.sum += sums.weight * (2.0f * abs(n) - 1.0f);
        break;
    case FractalType::Ridged: {
        float signal = params.offset - abs(n);
        signal *= signal * sums.feedback;
        sums.feedback = clamp(2.0f * signal, 0.0f, 1.0f);
        sums.sum += sums.weight * signal;
        break;
    }
    case FractalType::HybridMultifractal: {
        const float signal = (n + params.offset) * sums.weight;
        if (octave == 0) {
            sums.sum = signal;
            sums.feedback = signal;
        } else {
            sums.feedback = clamp(sums.feedback, 0.0f, 1.0f);
            sums.sum += sums.feedback * signal;
            sums.feedback *= signal;
        }
        break;
    }
    default:
        sums.sum += sums.weight * n;
        break;
    }
    sums.totalWeight += sums.weight;
    sums.weight *= params.gain;
    return sums;
}

/// The fractal height from its sums over every octave.
inline float FinishFractal(FractalSums sums, FractalParams params) {
    switch (params.type) {
    case FractalType::Ridged: {
        const float ridgeScale = max(params.offset, 1.0f - params.offset);
        return params.amplitude *
               (2.0f * sums.sum / (ridgeScale * ridgeScale * sums.totalWeight) - 1.0f);
    }
    case FractalType::HybridMultifractal:
        return params.amplitude * sums.sum / ((1.0f + params.offset) * sums.totalWeight);
    default:
        return params.amplitude * sums.sum / sums.totalWeight;
    }
}

}  // namespace detail

/// Fractal height at world position (`x`, `y`), in [-amplitude, amplitude] world units.
///
/// Each octave uses its own seed, a pseudo-random sub-cell lattice offset from `pcg3d` and a
/// lattice turned by the golden angle from the octave before; without the offset every octave's
/// lattice would share the origin, leaving a visible fixed point, and without the turn the creases
/// of ridged and billow fractals would line up along the lattice axes. Every fractal type is
/// normalized by its largest possible value so the bound holds for any parameters:
/// - `Fbm`: sum(w_i n_i) / sum(w_i).
/// - `Billow`: fBm of 2|n_i| - 1.
/// - `Ridged`: Musgrave's ridged multifractal, s_i = (offset - |n_i|)^2 weighted by the previous
///   octave's signal (feedback gain 2, clamped to [0, 1]), rescaled from [0, 1] to [-1, 1].
/// - `HybridMultifractal`: Musgrave's hybrid multifractal with (n_i + offset) signals and
///   the running weight clamped to [0, 1], divided by (1 + offset) * sum(w_i).
///
/// Parameters are assumed valid; `ValidateFractalParams` in terraingen/noise/validate.h checks
/// them on the host.
inline float Fractal2(float x, float y, FractalParams params) {
    float frequency = 1.0f / params.wavelength;
    float turnCos = 1.0f;
    float turnSin = 0.0f;
    detail::FractalSums sums = {};
    for (int octave = 0; octave < params.octaves; ++octave) {
        const UVec3 h = Pcg3dHash(detail::OctaveKey(params.seed, uint32_t(octave), 0u));
        const float offsetX = HashToUnitFloat(h.y);
        const float offsetY = HashToUnitFloat(h.z);
        const float rx = x * turnCos - y * turnSin;
        const float ry = x * turnSin + y * turnCos;
        const float n =
            BasisNoise2(params.basis, rx * frequency + offsetX, ry * frequency + offsetY, h.x);
        sums = detail::AddFractalOctave(sums, params, octave, n);
        frequency *= params.lacunarity;
        const float nextCos = turnCos * detail::kOctaveTurnCos - turnSin * detail::kOctaveTurnSin;
        turnSin = turnSin * detail::kOctaveTurnCos + turnCos * detail::kOctaveTurnSin;
        turnCos = nextCos;
    }
    return detail::FinishFractal(sums, params);
}

#ifndef __SLANG__
/// `Fractal2` in 3D at world position (`x`, `y`, `z`), for noise on a sphere evaluated at its 3D
/// points. Each octave's lattice coordinate is split into an integer cell and a fraction in double
/// precision, so noise stays exact at a planet's radius; only the bases with
/// `HasBasisNoise3` are used, others falling back to Perlin noise.
inline float Fractal3(double x, double y, double z, FractalParams params) {
    /// The golden angle about the axis (1, 2, 3) / sqrt(14), which lies along no lattice axis.
    static constexpr double kTurn[3][3] = {
        {-0.6132711010727254, -0.293401570821183, 0.7333580809050306},
        {0.7897926788435601, -0.24097777005594262, 0.5640542870894418},
        {0.011228581128535142, 0.9251190369776896, 0.37951111497202883}};
    double frequency = 1.0 / static_cast<double>(params.wavelength);
    double turn[3][3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    detail::FractalSums sums = {};
    for (int octave = 0; octave < params.octaves; ++octave) {
        const UVec3 h = Pcg3dHash(detail::OctaveKey(params.seed, uint32_t(octave), 0u));
        const UVec3 g = Pcg3dHash(detail::OctaveKey(params.seed, uint32_t(octave), 1u));
        const double rx = turn[0][0] * x + turn[0][1] * y + turn[0][2] * z;
        const double ry = turn[1][0] * x + turn[1][1] * y + turn[1][2] * z;
        const double rz = turn[2][0] * x + turn[2][1] * y + turn[2][2] * z;
        const double px = rx * frequency + static_cast<double>(HashToUnitFloat(h.y));
        const double py = ry * frequency + static_cast<double>(HashToUnitFloat(h.z));
        const double pz = rz * frequency + static_cast<double>(HashToUnitFloat(g.x));
        const float n = BasisNoise3(params.basis, SplitLatticePrecise3(px, py, pz), h.x);
        sums = detail::AddFractalOctave(sums, params, octave, n);
        frequency *= static_cast<double>(params.lacunarity);
        double next[3][3] = {};
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                next[row][column] = kTurn[row][0] * turn[0][column] +
                                    kTurn[row][1] * turn[1][column] +
                                    kTurn[row][2] * turn[2][column];
            }
        }
        std::memcpy(turn, next, sizeof(turn));
    }
    return detail::FinishFractal(sums, params);
}
#endif

}  // namespace terraingen
