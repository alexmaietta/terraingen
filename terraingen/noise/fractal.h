/// @file
/// @brief Fractals (fBm, ridged, billow, hybrid) built from octaves of a basis noise.
#pragma once

#include "terraingen/noise/hash.h"
#include "terraingen/noise/perlin.h"
#include "terraingen/noise/simplex.h"
#include "terraingen/noise/value.h"
#include "terraingen/noise/worley.h"

namespace terraingen {

/// Single-octave noise function a fractal is built from.
enum class NoiseBasis : uint32_t {
    Perlin = 0,           ///< Improved gradient noise (`PerlinNoise2`).
    Simplex = 1,          ///< Simplex noise (`SimplexNoise2`).
    Value = 2,            ///< Value noise (`ValueNoise2`).
    WorleyF1 = 3,         ///< Distance to the nearest feature point; cones and basins.
    WorleyF2 = 4,         ///< Distance to the second-nearest feature point.
    WorleyF2MinusF1 = 5,  ///< F2 - F1; ridges along cell borders, like cracked ground.
    Voronoi = 6,          ///< Value of the nearest feature point's cell; flat polygonal plateaus.
};

/// Number of `NoiseBasis` values.
static const uint32_t kNoiseBasisCount = 7;

/// How octaves of the basis noise are combined.
enum class FractalType : uint32_t {
    Fbm = 0,                 ///< Weighted sum of octaves (fractional Brownian motion).
    Ridged = 1,              ///< Ridged multifractal: sharp crests from inverted |noise|.
    Billow = 2,              ///< Sum of |noise| octaves: rounded, puffy forms.
    HybridMultifractal = 3,  ///< Smooth lowlands and rough highlands.
};

/// Number of `FractalType` values.
static const uint32_t kFractalTypeCount = 4;

/// Parameters of a fractal built from octaves of a basis noise.
///
/// Octave i has wavelength `wavelength / lacunarity^i` and weight `gain^i`;
/// `gain = lacunarity^(-H)` gives Hurst exponent H (`GainFromHurst`). Checked on the host by
/// `ValidateFractalParams`.
struct FractalParams {
    uint32_t seed = 0;           ///< Seed; each octave derives its own seed and lattice offset.
    float wavelength = 1000.0f;  ///< Wavelength of the first octave, in world units (metres).
    float amplitude = 100.0f;    ///< Maximum absolute height, in world units (metres).
    int octaves = 8;             ///< Number of octaves; at least 1.
    float lacunarity = 2.0f;     ///< Frequency ratio between successive octaves; greater than 1.
    float gain = 0.5f;           ///< Weight ratio between successive octaves; greater than 0.
    NoiseBasis basis = NoiseBasis::Perlin;  ///< Single-octave noise.
    FractalType type = FractalType::Fbm;    ///< How octaves combine.
    float offset = 1.0f;  ///< Ridge offset for `Ridged` and `HybridMultifractal`; > 0.

#ifndef __SLANG__
    bool operator==(const FractalParams&) const = default;
#endif
};

/// Octave gain `lacunarity^(-hurst)` for a Hurst exponent; evaluated on the host, kernels only
/// get the gain.
/// @param hurst Hurst exponent H, usually in (0, 1].
/// @param lacunarity Frequency ratio between octaves, > 1.
inline float GainFromHurst(float hurst, float lacunarity) {
    return pow(lacunarity, -hurst);
}

/// One octave of 2D `basis` noise; Worley distances are mapped linearly from [0, bound] to
/// [-1, 1], and Voronoi gives the cell value.
/// @param basis The basis; unknown values fall back to Perlin.
/// @return Noise in [-1, 1].
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
    case NoiseBasis::Voronoi:
        return Worley2(x, y, seed).value;
    default:
        return PerlinNoise2(x, y, seed);
    }
}

/// One octave of 3D `basis` noise, mapped like `BasisNoise2` with the 3D Worley bounds sqrt(3)
/// for F1 and sqrt(6) for F2; unknown values fall back to Perlin.
/// @return Noise in [-1, 1].
inline float BasisNoise3(NoiseBasis basis, LatticePoint3 point, uint32_t seed) {
    constexpr float kInvSqrt3 = 0.577350269189625765f;
    constexpr float kInvSqrt6 = 0.408248290463863016f;
    switch (basis) {
    case NoiseBasis::Simplex:
        return SimplexNoise3(point, seed);
    case NoiseBasis::Value:
        return ValueNoise3(point, seed);
    case NoiseBasis::WorleyF1:
        return 2.0f * min(Worley3(point, seed).f1 * kInvSqrt3, 1.0f) - 1.0f;
    case NoiseBasis::WorleyF2:
        return 2.0f * min(Worley3(point, seed).f2 * kInvSqrt6, 1.0f) - 1.0f;
    case NoiseBasis::WorleyF2MinusF1: {
        const WorleyDistances d = Worley3(point, seed);
        return 2.0f * min((d.f2 - d.f1) * kInvSqrt6, 1.0f) - 1.0f;
    }
    case NoiseBasis::Voronoi:
        return Worley3(point, seed).value;
    default:
        return PerlinNoise3(point, seed);
    }
}

namespace detail {

/// Cosine of the golden-angle turn between successive 2D octave lattices, accumulated by
/// multiplication for bit-identical results.
static const float kOctaveTurnCos = -0.7373688780783197f;
/// Sine of the golden-angle turn; see `kOctaveTurnCos`.
static const float kOctaveTurnSin = 0.6754902942615238f;

/// The running sums of a fractal over its octaves.
struct FractalSums {
    float weight = 1.0f;       ///< Weight of the next octave, `gain^i`.
    float sum = 0.0f;          ///< Weighted sum of octave signals so far.
    float totalWeight = 0.0f;  ///< Sum of the weights so far, for normalization.
    float feedback = 1.0f;     ///< Previous signal weighting the next (ridged and hybrid).
};

/// Adds one octave to a fractal's running sums.
/// @param octave The octave, from 0.
/// @param n The octave's basis noise, in [-1, 1].
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

/// The fractal height in [-amplitude, amplitude] world units from its sums over every octave,
/// normalized by its largest value.
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

/// 2D fractal height; each octave has its own seed, lattice offset and golden-angle turn.
/// @param x, y World coordinates in metres.
/// @param params The fractal, assumed valid (`ValidateFractalParams` checks it on the host).
/// @return Height in [-amplitude, amplitude] world units, for any parameters.
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

/// `Fractal3` in single precision, for kernels: the same octaves, turns and offsets, to within
/// float rounding for coordinates of moderate size, such as a crater's local frame.
/// @return Height in [-amplitude, amplitude] world units.
inline float Fractal3Float(float x, float y, float z, FractalParams params) {
    const float k00 = -0.6132711010727254f;
    const float k01 = -0.293401570821183f;
    const float k02 = 0.7333580809050306f;
    const float k10 = 0.7897926788435601f;
    const float k11 = -0.24097777005594262f;
    const float k12 = 0.5640542870894418f;
    const float k20 = 0.011228581128535142f;
    const float k21 = 0.9251190369776896f;
    const float k22 = 0.37951111497202883f;
    float t00 = 1.0f;
    float t01 = 0.0f;
    float t02 = 0.0f;
    float t10 = 0.0f;
    float t11 = 1.0f;
    float t12 = 0.0f;
    float t20 = 0.0f;
    float t21 = 0.0f;
    float t22 = 1.0f;
    float frequency = 1.0f / params.wavelength;
    detail::FractalSums sums = {};
    for (int octave = 0; octave < params.octaves; ++octave) {
        const UVec3 h = Pcg3dHash(detail::OctaveKey(params.seed, uint32_t(octave), 0u));
        const UVec3 g = Pcg3dHash(detail::OctaveKey(params.seed, uint32_t(octave), 1u));
        const float px = (t00 * x + t01 * y + t02 * z) * frequency + HashToUnitFloat(h.y);
        const float py = (t10 * x + t11 * y + t12 * z) * frequency + HashToUnitFloat(h.z);
        const float pz = (t20 * x + t21 * y + t22 * z) * frequency + HashToUnitFloat(g.x);
        const float n = BasisNoise3(params.basis, SplitLattice3(px, py, pz), h.x);
        sums = detail::AddFractalOctave(sums, params, octave, n);
        frequency *= params.lacunarity;
        const float n00 = k00 * t00 + k01 * t10 + k02 * t20;
        const float n01 = k00 * t01 + k01 * t11 + k02 * t21;
        const float n02 = k00 * t02 + k01 * t12 + k02 * t22;
        const float n10 = k10 * t00 + k11 * t10 + k12 * t20;
        const float n11 = k10 * t01 + k11 * t11 + k12 * t21;
        const float n12 = k10 * t02 + k11 * t12 + k12 * t22;
        const float n20 = k20 * t00 + k21 * t10 + k22 * t20;
        const float n21 = k20 * t01 + k21 * t11 + k22 * t21;
        const float n22 = k20 * t02 + k21 * t12 + k22 * t22;
        t00 = n00;
        t01 = n01;
        t02 = n02;
        t10 = n10;
        t11 = n11;
        t12 = n12;
        t20 = n20;
        t21 = n21;
        t22 = n22;
    }
    return detail::FinishFractal(sums, params);
}

#ifndef __SLANG__
/// `Fractal2` in 3D, for noise on a sphere; lattice coordinates are split in double
/// precision so noise stays exact at a planet's radius.
/// @param x, y, z World coordinates in metres.
/// @param params The fractal, assumed valid.
/// @return Height in [-amplitude, amplitude] world units.
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
