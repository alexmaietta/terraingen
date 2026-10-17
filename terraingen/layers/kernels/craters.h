/// @file
/// @brief The kernel of crater shaping (`ApplyCraters`); a kernel header for C++ and Slang.
///
/// The host prepares each crater (`CraterRecord`), fits its datum and gathers what varies in
/// length, outlines and diffusion tables, into one array of floats (`CraterData`); the kernel
/// shapes one sample from its offset to a crater's centre, in single precision.
#pragma once

#include "terraingen/noise/fractal.h"
#include "terraingen/util/portable.h"

namespace terraingen {

/// The fresh profile of simple craters.
enum class CraterProfileKind : uint32_t {
    FassettThomson = 0,       ///< `FassettThomsonProfile`.
    MartinParkesDunstan = 1,  ///< `MartinParkesDunstanProfile`.
};

/// A crater profile as the kernel evaluates it: a simple profile, complex craters from a
/// transition diameter (`ComplexCraterProfile`), and diffusion of the fresh shape by tables
/// (`DiffusedProfile`).
struct CraterProfileKernel {
    CraterProfileKind simple = CraterProfileKind::FassettThomson;
    uint32_t rim = 0;       ///< `FassettThomsonRim` as a number.
    uint32_t complex = 0;   ///< 1 when complex craters apply.
    uint32_t diffused = 0;  ///< 1 when the profile is diffused by tables.
    float transitionDiameter = 0.0f;
    float floorCoefficient = 0.0f;
    float floorExponent = 0.0f;
    float peakHeightCoefficient = 0.0f;
    float peakHeightExponent = 0.0f;
    float peakWidthCoefficient = 0.0f;
    float peakWidthExponent = 0.0f;
    uint32_t peakFromVolume = 0;  ///< 1 when the peak's base follows from its volume.
    float peakFitMaxDiameter = 0.0f;
    float ejectaExponent = 3.0f;
    float complexReachRadii = 3.0f;
    float diffusedExtent = 1.5f;  ///< Half the diffused domain's reach, in diameters.
    uint32_t diffusedCells = 0;
    uint32_t agesOffset = 0;  ///< Where the tabulated diffusion ages start in `CraterData`.
    uint32_t ageCount = 0;
};

/// How craters are shaped, as `CraterApplyOptions` and the profile give it.
struct CraterShapeKernel {
    CraterProfileKernel profile;
    float reachRadii = 1.5f;         ///< `CraterReachRadii`.
    float profileReachRadii = 1.5f;  ///< The profile's own reach.
    float blendStart = 0.8f;
    float blendEnd = 1.2f;
    uint32_t blendShape = 0;
    float interiorReplacement = 1.0f;
    float mantling = 0.0f;
    float ejectaReachRadii = 3.0f;
    float rimSigma = 0.0f;
    float rimInner = 0.8f;
    float rimOuter = 1.2f;
    float rimStrength = 1.0f;
    uint32_t rimShape = 0;
    float noiseAmplitude = 0.0f;
    float noiseWavelength = 0.5f;
    int noiseOctaves = 4;
    float noiseGain = 0.5f;
    float noiseRimAmplitude = 0.0f;
    float noiseRimWavelength = 0.1f;
    float noiseRimInner = 0.8f;
    float noiseRimOuter = 1.2f;
    float wallStreaks = 0.0f;
    float wallStreakWidth = 0.15f;
    float ejecta = 0.0f;
    float ejectaLineation = 0.15f;
    float minFreshness = 0.5f;
};

/// One crater ready to shape.
struct CraterRecord {
    float diameter = 0.0f;
    float resolution = 1.0f;
    float freshDepth = 0.0f;
    float freshRim = 0.0f;
    float currentDepth = 0.0f;
    float currentRim = 0.0f;
    float diffusion = 0.0f;
    float ellipticity = 1.0f;
    float heading = 0.0f;
    uint32_t firstHarmonic = 2;
    uint32_t harmonicCount = 0;
    uint32_t harmonicOffset = 0;  ///< Amplitude and phase pairs in `CraterData`.
    uint32_t tableCount = 0;
    uint32_t tableOffset = 0;      ///< The outline's deviations in `CraterData`.
    uint32_t diffusionOffset = 0;  ///< Its diffusion table in `CraterData`.
    uint32_t hasDatum = 0;
    float datumA = 0.0f;
    float datumB = 0.0f;
    float datumC = 0.0f;
    uint32_t noiseKey = 0;
    uint32_t textureKey = 0;
};

#ifdef __SLANG__
StructuredBuffer<float> gCraterData;

/// Where the variable-length values of craters are read from; a global buffer on the GPU.
struct CraterData {
    uint32_t unused;
};

inline float CraterValue(CraterData data, uint32_t index) {
    return gCraterData[index];
}
#else
/// Where the variable-length values of craters are read from.
struct CraterData {
    const float* values = nullptr;
};

inline float CraterValue(CraterData data, uint32_t index) {
    return data.values[index];
}
#endif

/// 1 - `t` shaped by `BlendShape` number `shape` (linear, smoothstep, smootherstep, Gaussian), for
/// `t` in [0, 1].
inline float KernelFalloffShape(float t, uint32_t shape) {
    if (shape == 0u) {
        return 1.0f - t;
    }
    if (shape == 1u) {
        return 1.0f - t * t * (3.0f - 2.0f * t);
    }
    if (shape == 2u) {
        return 1.0f - t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }
    const float tail = exp(-4.5f);
    const float z = 3.0f * t;
    return (exp(-0.5f * z * z) - tail) / (1.0f - tail);
}

/// 1 up to `start`, 0 from `end`, shaped by `shape` between (`Falloff`).
inline float KernelFalloff(float x, float start, float end, uint32_t shape) {
    if (x <= start) {
        return 1.0f;
    }
    if (x >= end) {
        return 0.0f;
    }
    return KernelFalloffShape((x - start) / (end - start), shape);
}

inline float KernelSmoothstep(float t) {
    const float u = clamp(t, 0.0f, 1.0f);
    return u * u * (3.0f - 2.0f * u);
}

/// `EjectaThicknessShare`.
inline float KernelEjectaShare(float radii, float reachRadii) {
    if (radii <= 1.0f) {
        return 1.0f;
    }
    if (radii >= reachRadii) {
        return 0.0f;
    }
    const float atReach = 1.0f / (reachRadii * reachRadii * reachRadii);
    return (1.0f / (radii * radii * radii) - atReach) / (1.0f - atReach);
}

/// `FassettThomsonProfile::Height` over the diameter.
inline float FassettThomsonShape(float x, uint32_t rim, float freshRim, float currentDepth) {
    if (x > 1.5f) {
        return 0.0f;
    }
    const float interior =
        -0.228809953f + x * (0.227533882f + x * (0.083116795f - x * 0.039499407f));
    const float exterior = 0.188253307f + x * (-0.187050452f + x * (0.01844746f + x * 0.01505647f));
    float h = 0.0f;
    if (rim == 2u) {
        if (x <= 0.98f) {
            h = interior;
        } else if (x <= 1.02f) {
            h = freshRim;
        } else {
            h = exterior;
        }
    } else {
        const float edge = rim == 0u ? 0.98f : 1.0f;
        if (x <= 0.2f) {
            h = -0.181f;
        } else if (x <= edge) {
            h = interior;
        } else {
            h = exterior;
        }
    }
    return max(h, freshRim - currentDepth);
}

/// `MartinParkesDunstanProfile::Height` (m); the host checks the state.
inline float MartinParkesDunstanHeight(float distance, float diameter, KERNEL_IN(CraterRecord) c) {
    const float depth = c.currentDepth * diameter;
    const float rim = c.currentRim * diameter;
    const float freshRim = c.freshRim * diameter;
    const float radius = 0.5f * diameter;
    const float x = distance / radius - 1.0f;
    if (x >= 1.0f) {
        return 0.0f;
    }
    const float k = freshRim - rim + depth;
    const float alpha = (rim - freshRim) / k;
    if (x <= alpha) {
        return (k * x + 2.0f * k) * x + freshRim;
    }
    if (x <= 0.0f) {
        return k * (alpha + 1.0f) / alpha * x * x + rim;
    }
    const float ejectaScale = 0.14f * pow(radius, 0.74f);
    const float offset = rim - ejectaScale;
    const float gradient = -ejectaScale / 8.0f - offset;
    const float beta = min(1.0f, 1.5f * (freshRim - rim) / k);
    const float atBeta =
        ejectaScale / ((beta + 1.0f) * (beta + 1.0f) * (beta + 1.0f)) + offset + gradient * beta;
    if (x <= beta) {
        const float a = (rim - atBeta) / (2.0f * beta * beta * beta);
        return (a * x - 3.0f * a * beta) * x * x + rim;
    }
    return ejectaScale / ((x + 1.0f) * (x + 1.0f) * (x + 1.0f)) + offset + gradient * x;
}

/// The simple profile's height (m) in the fresh state when `fresh` is 1.
inline float SimpleProfileHeight(KERNEL_IN(CraterProfileKernel) p, float distance, float diameter,
                                 KERNEL_IN(CraterRecord) c) {
    if (p.simple == CraterProfileKind::MartinParkesDunstan) {
        return MartinParkesDunstanHeight(distance, diameter, c);
    }
    return FassettThomsonShape(distance / (0.5f * diameter), p.rim, c.freshRim, c.currentDepth) *
           diameter;
}

/// y (m) of a power law `coefficient` D^`exponent` in kilometres, or m^3 with `volume`.
inline float KilometreLaw(float coefficient, float exponent, float diameter, bool volume) {
    const float kilometres = coefficient * pow(diameter / 1000.0f, exponent);
    return volume ? kilometres * 1e9f : kilometres * 1000.0f;
}

/// `ComplexCraterProfile::Height` of a complex crater (m).
inline float ComplexProfileHeight(KERNEL_IN(CraterProfileKernel) p, float distance, float diameter,
                                  KERNEL_IN(CraterRecord) c) {
    const float radius = 0.5f * diameter;
    const float x = distance / radius;
    const float depth = c.currentDepth * diameter;
    const float rim = c.currentRim * diameter;
    if (x >= 1.0f) {
        if (x >= p.complexReachRadii) {
            return 0.0f;
        }
        const float edge = pow(p.complexReachRadii, -p.ejectaExponent);
        return rim * (pow(x, -p.ejectaExponent) - edge) / (1.0f - edge);
    }
    const float floorShare =
        min(0.9f, KilometreLaw(p.floorCoefficient, p.floorExponent, diameter, false) / diameter);
    float height = rim - depth;
    if (x > floorShare) {
        const float t = (x - floorShare) / (1.0f - floorShare);
        height += depth * t * t;
    }
    const float fitted =
        p.peakFitMaxDiameter > 0.0f ? min(diameter, p.peakFitMaxDiameter) : diameter;
    const float peakHeight =
        KilometreLaw(p.peakHeightCoefficient, p.peakHeightExponent, fitted, false);
    float base = 0.0f;
    if (p.peakFromVolume == 1u) {
        const float volume =
            KilometreLaw(p.peakWidthCoefficient, p.peakWidthExponent, fitted, true);
        base = sqrt(3.0f * volume / (3.14159265358979f * peakHeight));
    } else {
        base = 0.5f * KilometreLaw(p.peakWidthCoefficient, p.peakWidthExponent, fitted, false);
    }
    base *= diameter / fitted;
    if (distance < base) {
        const float kept = c.freshDepth > 0.0f ? c.currentDepth / c.freshDepth : 1.0f;
        const float u = 1.0f - (distance / base) * (distance / base);
        height += kept * peakHeight * u * u;
    }
    return height;
}

/// The fresh profile's height (m): simple or complex.
inline float FreshProfileHeight(KERNEL_IN(CraterProfileKernel) p, float distance, float diameter,
                                KERNEL_IN(CraterRecord) c) {
    if (p.complex == 1u && diameter >= p.transitionDiameter) {
        return ComplexProfileHeight(p, distance, diameter, c);
    }
    return SimpleProfileHeight(p, distance, diameter, c);
}

/// `DiffusedProfile::Height` (m), reading the crater's table from `data`.
inline float DiffusedProfileHeight(KERNEL_IN(CraterProfileKernel) p, CraterData data,
                                   float distance, float diameter, KERNEL_IN(CraterRecord) c) {
    if (!(c.diffusion > 0.0f)) {
        CraterRecord fresh = c;
        fresh.currentDepth = c.freshDepth;
        fresh.currentRim = c.freshRim;
        return FreshProfileHeight(p, distance, diameter, fresh);
    }
    const float x = distance / diameter;
    if (x >= p.diffusedExtent) {
        return 0.0f;
    }
    const float cell = x / p.diffusedExtent * float(p.diffusedCells);
    const uint32_t i = min(uint32_t(cell), p.diffusedCells - 1u);
    const float t = cell - float(i);
    uint32_t row = 0u;
    float weight = 0.0f;
    const uint32_t last = p.ageCount - 1u;
    const float first = CraterValue(data, p.agesOffset + 1u);
    if (c.diffusion >= CraterValue(data, p.agesOffset + last)) {
        row = last;
    } else if (c.diffusion < first) {
        weight = c.diffusion / first;
    } else {
        uint32_t low = 1u;
        uint32_t high = last;
        while (high - low > 1u) {
            const uint32_t middle = (low + high) / 2u;
            if (CraterValue(data, p.agesOffset + middle) <= c.diffusion) {
                low = middle;
            } else {
                high = middle;
            }
        }
        row = low;
        const float below = CraterValue(data, p.agesOffset + low);
        const float above = CraterValue(data, p.agesOffset + high);
        weight = log(c.diffusion / below) / log(above / below);
    }
    const uint32_t width = p.diffusedCells + 1u;
    const uint32_t at = c.diffusionOffset + row * width + i;
    float value = (1.0f - t) * CraterValue(data, at) + t * CraterValue(data, at + 1u);
    if (weight > 0.0f) {
        const float next =
            (1.0f - t) * CraterValue(data, at + width) + t * CraterValue(data, at + width + 1u);
        value += weight * (next - value);
    }
    return diameter * value;
}

/// The profile's height (m) at `distance`.
inline float KernelProfileHeight(KERNEL_IN(CraterShapeKernel) k, CraterData data, float distance,
                                 float diameter, KERNEL_IN(CraterRecord) c) {
    if (k.profile.diffused == 1u) {
        return DiffusedProfileHeight(k.profile, data, distance, diameter, c);
    }
    return FreshProfileHeight(k.profile, distance, diameter, c);
}

/// The profile's height with its rim rounded (`RimSmoothing`), by 9-point Gauss-Hermite.
inline float KernelRoundedHeight(KERNEL_IN(CraterShapeKernel) k, CraterData data, float distance,
                                 float diameter, KERNEL_IN(CraterRecord) c) {
    const float height = KernelProfileHeight(k, data, distance, diameter, c);
    if (k.rimSigma == 0.0f) {
        return height;
    }
    const float radius = 0.5f * diameter;
    const float radii = distance / radius;
    const float weight =
        radii <= 1.0f
            ? k.rimStrength * KernelFalloff(2.0f - radii, 1.0f, 2.0f - k.rimInner, k.rimShape)
            : k.rimStrength * KernelFalloff(radii, 1.0f, k.rimOuter, k.rimShape);
    if (weight == 0.0f) {
        return height;
    }
    const float nodes[5] = {0.0f, 0.723551018752838f, 1.46855328921667f, 2.26658058453184f,
                            3.19099320178153f};
    const float weights[5] = {0.720235215606051f, 0.432651559002556f, 0.0884745273943766f,
                              0.00494362427553695f, 0.0000396069772632645f};
    const float step = 1.41421356237310f * k.rimSigma * radius;
    float smoothed = weights[0] * height;
    for (int i = 1; i < 5; ++i) {
        smoothed +=
            weights[i] * KernelProfileHeight(k, data, abs(distance - step * nodes[i]), diameter, c);
        smoothed +=
            weights[i] * KernelProfileHeight(k, data, abs(distance + step * nodes[i]), diameter, c);
    }
    smoothed /= 1.77245385090552f;
    return height + weight * (smoothed - height);
}

/// The crater noise (`CraterNoise`) at `dx`, `dy` metres from the centre and `radii` radii out.
inline float KernelNoiseHeight(KERNEL_IN(CraterShapeKernel) k, KERNEL_IN(CraterRecord) c, float dx,
                               float dy, float radii) {
    if (k.noiseAmplitude == 0.0f && k.noiseRimAmplitude == 0.0f) {
        return 0.0f;
    }
    const float radius = 0.5f * c.diameter;
    const float footprint =
        radii <= 1.0f ? 1.0f : KernelFalloff(radii, 1.0f, k.profileReachRadii, 2u);
    const float rim = radii <= 1.0f ? KernelFalloff(2.0f - radii, 1.0f, 2.0f - k.noiseRimInner, 2u)
                                    : KernelFalloff(radii, 1.0f, k.noiseRimOuter, 2u);
    float height = 0.0f;
    FractalParams params;
    params.amplitude = 1.0f;
    params.octaves = k.noiseOctaves;
    params.gain = k.noiseGain;
    if (k.noiseAmplitude > 0.0f && footprint > 0.0f) {
        params.seed = c.noiseKey;
        params.wavelength = k.noiseWavelength * radius;
        height += k.noiseAmplitude * footprint * Fractal2(dx, dy, params);
    }
    if (k.noiseRimAmplitude > 0.0f && rim > 0.0f) {
        params.seed = c.noiseKey + 1u;
        params.wavelength = k.noiseRimWavelength * radius;
        height += k.noiseRimAmplitude * rim * Fractal2(dx, dy, params);
    }
    return c.diameter * height;
}

#ifndef __SLANG__
/// Fractal noise streaked radially (`RadialStreaks`); on the CPU only until the GPU has
/// three-dimensional fractals.
inline float KernelRadialStreaks(float radii, float angle, float across, float along, int octaves,
                                 uint32_t seed) {
    FractalParams params;
    params.seed = seed;
    params.wavelength = 1.0f;
    params.amplitude = 1.0f;
    params.octaves = octaves;
    const float ring = 1.0f / across;
    return Fractal3(ring * cos(angle), ring * sin(angle), radii / along, params);
}

/// The wall gullies and ejecta texture (`CraterTexture`).
inline float KernelTextureHeight(KERNEL_IN(CraterShapeKernel) k, KERNEL_IN(CraterRecord) c,
                                 float dx, float dy, float radii) {
    if (k.wallStreaks == 0.0f && k.ejecta == 0.0f) {
        return 0.0f;
    }
    const float freshness = c.freshDepth > 0.0f ? c.currentDepth / c.freshDepth : 1.0f;
    const float fade = KernelSmoothstep((freshness - k.minFreshness) / (1.0f - k.minFreshness));
    if (!(fade > 0.0f)) {
        return 0.0f;
    }
    const float angle = atan2(dy, dx);
    float height = 0.0f;
    if (k.wallStreaks > 0.0f && radii > 0.25f && radii < 1.0f) {
        const float band =
            KernelSmoothstep((radii - 0.25f) / 0.35f) * KernelSmoothstep((1.0f - radii) / 0.1f);
        height += k.wallStreaks * c.diameter * band *
                  KernelRadialStreaks(radii, angle, k.wallStreakWidth, 0.5f, 2, c.textureKey);
    }
    if (k.ejecta > 0.0f && radii > 1.0f && radii < k.ejectaReachRadii) {
        const float radius = 0.5f * c.diameter;
        const float thickness =
            0.14f * pow(radius, 0.74f) * KernelEjectaShare(radii, k.ejectaReachRadii);
        const float rise = KernelSmoothstep((radii - 1.0f) / 0.1f);
        height += k.ejecta * thickness * rise *
                  KernelRadialStreaks(radii, angle, k.ejectaLineation, 2.0f * k.ejectaLineation, 3,
                                      c.textureKey + 1u);
    }
    return fade * height;
}
#endif

/// How far the outline reaches toward (`dx`, `dy`), over the round radius (`CraterOutline::Scale`).
inline float KernelOutlineScale(CraterData data, KERNEL_IN(CraterRecord) c, float dx, float dy) {
    const float theta = atan2(dy, dx);
    float scale = 1.0f;
    if (c.ellipticity != 1.0f) {
        const float along = sqrt(c.ellipticity);
        const float across = 1.0f / along;
        const float u = cos(theta - c.heading) / along;
        const float v = sin(theta - c.heading) / across;
        scale = 1.0f / sqrt(u * u + v * v);
    }
    float wobble = 1.0f;
    if (c.tableCount > 0u) {
        const float turns = theta / 6.28318530717959f;
        const float position = (turns - floor(turns)) * float(c.tableCount);
        const uint32_t index = min(uint32_t(position), c.tableCount - 1u);
        const float t = position - float(index);
        wobble += (1.0f - t) * CraterValue(data, c.tableOffset + index) +
                  t * CraterValue(data, c.tableOffset + (index + 1u) % c.tableCount);
    }
    for (uint32_t n = 0u; n < c.harmonicCount; ++n) {
        const float order = float(c.firstHarmonic + n);
        wobble += CraterValue(data, c.harmonicOffset + 2u * n) *
                  cos(order * theta + CraterValue(data, c.harmonicOffset + 2u * n + 1u));
    }
    return scale * wobble;
}

/// `height` after crater `c`, at `dx` east and `dy` north of its centre in metres
/// (`ApplyCraterTo`); unchanged beyond its reach.
inline float ApplyCraterKernel(KERNEL_IN(CraterShapeKernel) k, CraterData data,
                               KERNEL_IN(CraterRecord) c, float height, float dx, float dy) {
    const float diameter = c.diameter;
    const float radius = 0.5f * diameter;
    float distance = sqrt(dx * dx + dy * dy);
    if (c.ellipticity != 1.0f || c.harmonicCount > 0u || c.tableCount > 0u) {
        distance /= KernelOutlineScale(data, c, dx, dy);
    }
    if (distance >= k.reachRadii * radius) {
        return height;
    }
    const float radii = distance / radius;
    float replaced = 0.0f;
    if (c.hasDatum == 1u) {
        float weight =
            k.interiorReplacement * KernelFalloff(radii, k.blendStart, k.blendEnd, k.blendShape);
        if (k.mantling > 0.0f && radii > 1.0f) {
            weight = max(weight, k.mantling * KernelEjectaShare(radii, k.ejectaReachRadii));
        }
        replaced = weight * (c.datumA + c.datumB * dx + c.datumC * dy - height);
    }
    float shaped = KernelRoundedHeight(k, data, distance, diameter, c) +
                   KernelNoiseHeight(k, c, dx, dy, radii);
#ifndef __SLANG__
    shaped += KernelTextureHeight(k, c, dx, dy, radii);
#endif
    return height + c.resolution * shaped + c.resolution * replaced;
}

}  // namespace terraingen
