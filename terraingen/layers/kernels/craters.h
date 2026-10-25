// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The kernel of crater shaping (`ApplyCraters`); a kernel header for C++ and Slang.
///
/// The host prepares each crater (`CraterRecord`), fits its datum and gathers what varies in
/// length, outlines and diffusion tables, into one array of floats (`CraterData`); the kernel
/// shapes one sample from its offset to a crater's center, in single precision.
#pragma once

#include "terraingen/noise/fractal.h"
#include "terraingen/util/portable.h"

#ifndef __SLANG__
#include "terraingen/gpu/program.h"
#endif

namespace terraingen {

/// The fresh profile of simple craters, or a profile tabulated per crater.
enum class CraterProfileKind : uint32_t {
    FassettThomson = 0,       ///< `FassettThomsonProfile`.
    MartinParkesDunstan = 1,  ///< `MartinParkesDunstanProfile`.
    /// Any other profile, tabulated per crater in its state: one row of heights over the
    /// diameter, `tableCells` + 1 of them from the center to `tableExtent` diameters out.
    Tabulated = 2,
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
    float tableExtent = 1.5f;  ///< Where tabulated heights end, in diameters from the center.
    uint32_t tableCells = 0;   ///< Cells of a tabulated profile row; rows hold one more height.
    uint32_t agesOffset = 0;   ///< Where the tabulated diffusion ages start in `CraterData`.
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
    float x = 0.0f;  ///< Center east of the dispatch's origin (m); read on the GPU only.
    float y = 0.0f;  ///< Center north of the dispatch's origin (m); read on the GPU only.
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
    uint32_t tableOffset = 0;    ///< The outline's deviations in `CraterData`.
    uint32_t profileOffset = 0;  ///< Its diffusion or profile table in `CraterData`.
    uint32_t hasDatum = 0;
    float datumA = 0.0f;
    float datumB = 0.0f;
    float datumC = 0.0f;
    uint32_t noiseKey = 0;
    uint32_t textureKey = 0;
    uint32_t markCount = 0;
    /// The marks' inner, outer and fade radii, then their knots, in `CraterData`.
    uint32_t markOffset = 0;
    float shiftX = 0.0f;    ///< `CraterSlopeShape::shift` east (m).
    float shiftY = 0.0f;    ///< `CraterSlopeShape::shift` north (m).
    float upslopeX = 0.0f;  ///< `CraterSlopeShape::upslope` east.
    float upslopeY = 0.0f;  ///< `CraterSlopeShape::upslope` north.
};

/// A point east and north of a crater's center, in meters, as the kernel reads it.
struct KernelOffset {
    float dx = 0.0f;
    float dy = 0.0f;
};

/// A crater's local frame on a body, from which the GPU works out each sample's offset
/// (`KernelFrameOffset`) as `Surface::Offset` does.
struct CraterFrame {
    float x = 0.0f;  ///< Center's position from the dispatch's origin (m), as `Surface::Position`.
    float y = 0.0f;
    float z = 0.0f;
    float eastX = 1.0f;  ///< Unit vector east at the center.
    float eastY = 0.0f;
    float eastZ = 0.0f;
    float northX = 0.0f;  ///< Unit vector north at the center.
    float northY = 1.0f;
    float northZ = 0.0f;
    /// The radius (m) of a sphere, whose offsets are azimuthal equidistant; 0 on an ellipsoid or
    /// shape model, whose offsets are projected on the tangent plane.
    float sphereRadius = 0.0f;
};

/// asin(`s`) / `s` for `s` in [0, 1]: by its series below 0.25, to within float rounding, since
/// GPUs may evaluate inverse trigonometric functions to only a few thousand ulp (Vulkan allows
/// 4096), which would be millimeters over a crater.
inline float KernelArcsineRatio(float s) {
    if (s < 0.25f) {
        const float s2 = s * s;
        return 1.0f + s2 * (1.0f / 6.0f +
                            s2 * (3.0f / 40.0f + s2 * (5.0f / 112.0f + s2 * (35.0f / 1152.0f))));
    }
    return atan2(s, sqrt(1.0f - s * s)) / s;
}

/// The offset of the point at position (`px`, `py`, `pz`) from the dispatch's origin (m) in
/// crater frame `f`: `Surface::Offset` in single precision.
inline KernelOffset KernelFrameOffset(KERNEL_IN(CraterFrame) f, float px, float py, float pz) {
    const float dx = px - f.x;
    const float dy = py - f.y;
    const float dz = pz - f.z;
    KernelOffset offset;
    offset.dx = dx * f.eastX + dy * f.eastY + dz * f.eastZ;
    offset.dy = dx * f.northX + dy * f.northY + dz * f.northZ;
    if (f.sphereRadius > 0.0f) {
        const float chord = sqrt(dx * dx + dy * dy + dz * dz);
        const float arc = chord * KernelArcsineRatio(min(0.5f * chord / f.sphereRadius, 1.0f));
        const float across = sqrt(offset.dx * offset.dx + offset.dy * offset.dy);
        if (across > 0.0f) {
            offset.dx *= arc / across;
            offset.dy *= arc / across;
        } else {
            offset.dy = arc;
        }
    }
    return offset;
}

#ifdef __SLANG__
/// Where the variable-length values of craters are read from: a buffer the dispatch binds.
typedef StructuredBuffer<float> CraterData;

inline float CraterValue(CraterData data, uint32_t index) {
    return data[index];
}

/// Crater records and frames, floats and unsigned integers a dispatch binds, indexed as arrays.
typedef StructuredBuffer<CraterRecord> CraterRecords;
typedef StructuredBuffer<CraterFrame> CraterFrames;
typedef StructuredBuffer<float> KernelFloats;
typedef StructuredBuffer<uint> KernelUints;
#else
/// Crater records and frames, floats and unsigned integers, indexed as arrays.
using CraterRecords = const CraterRecord*;
using CraterFrames = const CraterFrame*;
using KernelFloats = const float*;
using KernelUints = const uint32_t*;

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

/// The direction of (`dx`, `dy`) in radians from east, 0 at the origin: fast math on some GPUs
/// leaves `atan2(0, 0)` undefined.
inline float KernelAngle(float dy, float dx) {
    if (dx == 0.0f && dy == 0.0f) {
        return 0.0f;
    }
    return atan2(dy, dx);
}

/// pi and 2 pi in single precision.
static const float kKernelPi = 3.14159265358979f;
static const float kKernelTwoPi = 6.28318530717959f;

/// A periodic table's knot below an angle and the fraction of the way to the next.
struct KernelPeriodicCell {
    uint32_t index;
    float t;
};

/// Where angle `turns`, in turns, falls in a periodic table of `size` knots around a crater.
inline KernelPeriodicCell KernelPeriodicAt(float turns, uint32_t size) {
    const float position = (turns - floor(turns)) * float(size);
    KernelPeriodicCell cell;
    cell.index = min(uint32_t(position), size - 1u);
    cell.t = position - float(cell.index);
    return cell;
}

/// McGetchin et al.'s (1973) ejecta thickness (m) at the rim of a crater of `radius` meters.
inline float KernelEjectaScale(float radius) {
    return 0.14f * pow(radius, 0.74f);
}

/// McGetchin et al.'s (1973) ejecta thickness at `radii` crater radii over that at the rim,
/// (r / R)^-3, tapered linearly in it to nothing at `reachRadii`; 1 within the rim.
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

/// A profile's height (m) and its slope over the radius at a point.
struct KernelProfilePoint {
    float height;
    float slope;
};

/// Martin, Parkes and Dunstan's ejecta outside the rim, `x` radii beyond it in [0, 1]: the power
/// law of McGetchin et al. with a linear term that meets `rim` (m) at the crest and 0 at twice the
/// radius, tapered by 1 - x^2 so its slope also falls to 0 there and the crater joins the ground
/// smoothly; the taper leaves the height and slope at the crest unchanged.
inline KernelProfilePoint MartinParkesDunstanExterior(float x, float rim, float ejectaScale) {
    const float offset = rim - ejectaScale;
    const float gradient = -ejectaScale / 8.0f - offset;
    const float r = 1.0f / (x + 1.0f);
    const float law = ejectaScale * r * r * r + offset + gradient * x;
    const float lawSlope = -3.0f * ejectaScale * r * r * r * r + gradient;
    const float taper = 1.0f - x * x;
    KernelProfilePoint point;
    point.height = law * taper;
    point.slope = lawSlope * taper - 2.0f * x * law;
    return point;
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
    const float ejectaScale = KernelEjectaScale(radius);
    const float beta = min(1.0f, 1.5f * (freshRim - rim) / k);
    if (x <= beta) {
        const KernelProfilePoint join = MartinParkesDunstanExterior(beta, rim, ejectaScale);
        const float drop = rim - join.height;
        const float slope = drop > 0.0f ? max(join.slope, -3.0f * drop / beta) : join.slope;
        const float t = x / beta;
        return rim + (join.height - rim) * t * t * (3.0f - 2.0f * t) +
               beta * slope * t * t * (t - 1.0f);
    }
    return MartinParkesDunstanExterior(x, rim, ejectaScale).height;
}

/// The simple profile's height (m) in crater `c`'s current state.
inline float SimpleProfileHeight(KERNEL_IN(CraterProfileKernel) p, float distance, float diameter,
                                 KERNEL_IN(CraterRecord) c) {
    if (p.simple == CraterProfileKind::MartinParkesDunstan) {
        return MartinParkesDunstanHeight(distance, diameter, c);
    }
    return FassettThomsonShape(distance / (0.5f * diameter), p.rim, c.freshRim, c.currentDepth) *
           diameter;
}

/// y (m) of a power law `coefficient` D^`exponent` in kilometers, or m^3 with `volume`.
inline float KilometerLaw(float coefficient, float exponent, float diameter, bool volume) {
    const float kilometers = coefficient * pow(diameter / 1000.0f, exponent);
    return volume ? kilometers * 1e9f : kilometers * 1000.0f;
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
        min(0.9f, KilometerLaw(p.floorCoefficient, p.floorExponent, diameter, false) / diameter);
    float height = rim - depth;
    if (x > floorShare) {
        const float t = (x - floorShare) / (1.0f - floorShare);
        height += depth * t * t;
    }
    const float fitted =
        p.peakFitMaxDiameter > 0.0f ? min(diameter, p.peakFitMaxDiameter) : diameter;
    const float peakHeight =
        KilometerLaw(p.peakHeightCoefficient, p.peakHeightExponent, fitted, false);
    float base = 0.0f;
    if (p.peakFromVolume == 1u) {
        const float volume =
            KilometerLaw(p.peakWidthCoefficient, p.peakWidthExponent, fitted, true);
        base = sqrt(3.0f * volume / (kKernelPi * peakHeight));
    } else {
        base = 0.5f * KilometerLaw(p.peakWidthCoefficient, p.peakWidthExponent, fitted, false);
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

/// A table row's cell at a distance and the fraction of the way across it.
struct KernelTableCell {
    uint32_t index;
    float t;
};

/// Where `x` diameters from the center falls in a row of `p`'s tables.
inline KernelTableCell KernelTableAt(KERNEL_IN(CraterProfileKernel) p, float x) {
    const float position = x / p.tableExtent * float(p.tableCells);
    KernelTableCell cell;
    cell.index = min(uint32_t(position), p.tableCells - 1u);
    cell.t = position - float(cell.index);
    return cell;
}

/// The value `t` of the way from `data[at]` to `data[at + 1]`.
inline float KernelLerp(CraterData data, uint32_t at, float t) {
    return (1.0f - t) * CraterValue(data, at) + t * CraterValue(data, at + 1u);
}

/// The table row below a diffusion age and the weight of the row above it.
struct KernelAgeRow {
    uint32_t row;
    float weight;
};

/// Where diffusion age `tau` falls among `p`'s tabulated ages, interpolating in log tau (linearly
/// in tau before the first nonzero age), as `DiffusedProfile` does.
inline KernelAgeRow KernelAgeRowAt(KERNEL_IN(CraterProfileKernel) p, CraterData data, float tau) {
    KernelAgeRow age;
    age.row = 0u;
    age.weight = 0.0f;
    const uint32_t last = p.ageCount - 1u;
    const float first = CraterValue(data, p.agesOffset + 1u);
    if (tau >= CraterValue(data, p.agesOffset + last)) {
        age.row = last;
    } else if (tau < first) {
        age.weight = tau / first;
    } else {
        uint32_t low = 1u;
        uint32_t high = last;
        while (high - low > 1u) {
            const uint32_t middle = (low + high) / 2u;
            if (CraterValue(data, p.agesOffset + middle) <= tau) {
                low = middle;
            } else {
                high = middle;
            }
        }
        age.row = low;
        const float below = CraterValue(data, p.agesOffset + low);
        const float above = CraterValue(data, p.agesOffset + high);
        age.weight = log(tau / below) / log(above / below);
    }
    return age;
}

/// A diffused profile's height over the diameter at `x` diameters from the center, below
/// `p.tableExtent`, at diffusion age `tau`: the ages read from `ages` and the crater's rows, one
/// per age, from `table` at `rows`, interpolated linearly in distance and log age.
inline float KernelDiffusedValue(KERNEL_IN(CraterProfileKernel) p, CraterData ages,
                                 CraterData table, uint32_t rows, float x, float tau) {
    const KernelTableCell cell = KernelTableAt(p, x);
    const KernelAgeRow age = KernelAgeRowAt(p, ages, tau);
    const uint32_t width = p.tableCells + 1u;
    const uint32_t at = rows + age.row * width + cell.index;
    float value = KernelLerp(table, at, cell.t);
    if (age.weight > 0.0f) {
        value += age.weight * (KernelLerp(table, at + width, cell.t) - value);
    }
    return value;
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
    if (x >= p.tableExtent) {
        return 0.0f;
    }
    return diameter * KernelDiffusedValue(p, data, data, c.profileOffset, x, c.diffusion);
}

/// The height (m) of a profile tabulated for crater `c` (`CraterProfileKind::Tabulated`),
/// interpolated linearly along its row; 0 beyond the row.
inline float TabulatedProfileHeight(KERNEL_IN(CraterProfileKernel) p, CraterData data,
                                    float distance, float diameter, KERNEL_IN(CraterRecord) c) {
    const float x = distance / diameter;
    if (!(x < p.tableExtent)) {
        return 0.0f;
    }
    const KernelTableCell cell = KernelTableAt(p, x);
    return diameter * KernelLerp(data, c.profileOffset + cell.index, cell.t);
}

/// The profile's height (m) at `distance`.
inline float KernelProfileHeight(KERNEL_IN(CraterShapeKernel) k, CraterData data, float distance,
                                 float diameter, KERNEL_IN(CraterRecord) c) {
    if (k.profile.simple == CraterProfileKind::Tabulated) {
        return TabulatedProfileHeight(k.profile, data, distance, diameter, c);
    }
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

/// Fractal parameters with every field set, as kernels need: Slang does not apply a struct's
/// default values to a local.
inline FractalParams KernelFractalParams(uint32_t seed, float wavelength, int octaves, float gain) {
    FractalParams params;
    params.seed = seed;
    params.wavelength = wavelength;
    params.amplitude = 1.0f;
    params.octaves = octaves;
    params.lacunarity = 2.0f;
    params.gain = gain;
    params.basis = NoiseBasis::Perlin;
    params.type = FractalType::Fbm;
    params.offset = 1.0f;
    return params;
}

/// The crater noise (`CraterNoise`) at `dx`, `dy` meters from the center and `radii` radii out.
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
    if (k.noiseAmplitude > 0.0f && footprint > 0.0f) {
        const FractalParams params = KernelFractalParams(c.noiseKey, k.noiseWavelength * radius,
                                                         k.noiseOctaves, k.noiseGain);
        height += k.noiseAmplitude * footprint * Fractal2(dx, dy, params);
    }
    if (k.noiseRimAmplitude > 0.0f && rim > 0.0f) {
        const FractalParams params = KernelFractalParams(
            c.noiseKey + 1u, k.noiseRimWavelength * radius, k.noiseOctaves, k.noiseGain);
        height += k.noiseRimAmplitude * rim * Fractal2(dx, dy, params);
    }
    return c.diameter * height;
}

/// Fractal noise streaked radially (`RadialStreaks`).
inline float KernelRadialStreaks(float radii, float angle, float across, float along, int octaves,
                                 uint32_t seed) {
    const FractalParams params = KernelFractalParams(seed, 1.0f, octaves, 0.5f);
    const float ring = 1.0f / across;
    return Fractal3Float(ring * cos(angle), ring * sin(angle), radii / along, params);
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
    const float angle = KernelAngle(dy, dx);
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
            KernelEjectaScale(radius) * KernelEjectaShare(radii, k.ejectaReachRadii);
        const float rise = KernelSmoothstep((radii - 1.0f) / 0.1f);
        height += k.ejecta * thickness * rise *
                  KernelRadialStreaks(radii, angle, k.ejectaLineation, 2.0f * k.ejectaLineation, 3,
                                      c.textureKey + 1u);
    }
    return fade * height;
}

/// How far an area-preserving ellipse of long over short axis `ellipticity`, its long axis at
/// `heading` radians, reaches at angle `theta`, over the round radius.
inline float KernelEllipseScale(float theta, float ellipticity, float heading) {
    const float along = sqrt(ellipticity);
    const float across = 1.0f / along;
    const float u = cos(theta - heading) / along;
    const float v = sin(theta - heading) / across;
    return 1.0f / sqrt(u * u + v * v);
}

/// The periodic Catmull-Rom spline through knots `p0` to `p3` at `t` of the way from `p1` to `p2`.
inline float KernelCatmullRom(float p0, float p1, float p2, float p3, float t) {
    return 0.5f * (2.0f * p1 + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t +
                   (3.0f * (p1 - p2) + p3 - p0) * t * t * t);
}

/// The strength of wall marks at `radii`: full from `inner` to `outer` radii, fading along
/// smoothsteps `fade` radii wide either side, 0 beyond.
inline float KernelMarksWindow(float radii, float inner, float outer, float fade) {
    if (!(radii > inner - fade) || !(radii < outer + fade)) {
        return 0.0f;
    }
    return KernelSmoothstep((radii - inner + fade) / fade) *
           KernelSmoothstep((outer + fade - radii) / fade);
}

/// The outline's reach with its rough wobble kept by `share`, from 0 (none) to 1 (all): the rim
/// table by `share` and each harmonic of order k by `share`^(k / 2), so the short ones fade first.
inline float KernelOutlineScaleAt(CraterData data, KERNEL_IN(CraterRecord) c, float dx, float dy,
                                  float share) {
    const float theta = KernelAngle(dy, dx);
    const float scale =
        c.ellipticity != 1.0f ? KernelEllipseScale(theta, c.ellipticity, c.heading) : 1.0f;
    float wobble = 1.0f;
    if (c.tableCount > 0u && share > 0.0f) {
        const KernelPeriodicCell cell = KernelPeriodicAt(theta / kKernelTwoPi, c.tableCount);
        wobble +=
            share * ((1.0f - cell.t) * CraterValue(data, c.tableOffset + cell.index) +
                     cell.t * CraterValue(data, c.tableOffset + (cell.index + 1u) % c.tableCount));
    }
    if (share > 0.0f) {
        for (uint32_t n = 0u; n < c.harmonicCount; ++n) {
            const float order = float(c.firstHarmonic + n);
            const float kept = share < 1.0f ? pow(share, 0.5f * order) : 1.0f;
            wobble += kept * CraterValue(data, c.harmonicOffset + 2u * n) *
                      cos(order * theta + CraterValue(data, c.harmonicOffset + 2u * n + 1u));
        }
    }
    return scale * wobble;
}

/// How far the outline reaches toward (`dx`, `dy`), over the round radius (`CraterOutline::Scale`).
inline float KernelOutlineScale(CraterData data, KERNEL_IN(CraterRecord) c, float dx, float dy) {
    return KernelOutlineScaleAt(data, c, dx, dy, 1.0f);
}

/// The share of an outline's rough wobble a crater's profile follows `radii` from its center:
/// none at the center, rising along a smoothstep to all of it at the rim and beyond, so the floor
/// stays round and rim kinks fade down the wall instead of running to the center.
inline float KernelOutlineShare(float radii) {
    return KernelSmoothstep(radii);
}

/// The shift in radii crater `c`'s wall marks add at `radii` toward (`dx`, `dy`)
/// (`CraterOutline::MarkShift`).
inline float KernelMarkShift(CraterData data, KERNEL_IN(CraterRecord) c, float dx, float dy,
                             float radii) {
    const float window = KernelMarksWindow(radii, CraterValue(data, c.markOffset),
                                           CraterValue(data, c.markOffset + 1u),
                                           CraterValue(data, c.markOffset + 2u));
    if (window == 0.0f) {
        return 0.0f;
    }
    const uint32_t size = c.markCount;
    const KernelPeriodicCell cell = KernelPeriodicAt(KernelAngle(dy, dx) / kKernelTwoPi, size);
    const uint32_t index = cell.index;
    const uint32_t knots = c.markOffset + 3u;
    return window * KernelCatmullRom(CraterValue(data, knots + (index + size - 1u) % size),
                                     CraterValue(data, knots + index),
                                     CraterValue(data, knots + (index + 1u) % size),
                                     CraterValue(data, knots + (index + 2u) % size), cell.t);
}

/// The distance (m) crater `c`'s round profile is read at, `dx` east and `dy` north of its center
/// (`CraterProfileDistance`).
inline float KernelProfileDistance(CraterData data, KERNEL_IN(CraterRecord) c, float dx, float dy) {
    float distance = sqrt(dx * dx + dy * dy);
    if (c.ellipticity != 1.0f || c.harmonicCount > 0u || c.tableCount > 0u || c.markCount > 0u) {
        distance /= KernelOutlineScaleAt(data, c, dx, dy,
                                         KernelOutlineShare(distance / (0.5f * c.diameter)));
    }
    if (c.markCount > 0u) {
        const float radius = 0.5f * c.diameter;
        distance += radius * KernelMarkShift(data, c, dx, dy, distance / radius);
    }
    return distance;
}

/// How much of crater `c`'s floor shift applies at `dx` east and `dy` north of its center
/// (`CraterSlopeShape::shift`): all within 0.3 radii, fading along a smoothstep to none at the rim.
inline float KernelSlopeShiftWeight(KERNEL_IN(CraterRecord) c, float dx, float dy) {
    const float radii = sqrt(dx * dx + dy * dy) / (0.5f * c.diameter);
    return 1.0f - KernelSmoothstep((radii - 0.3f) / 0.7f);
}

/// The share of crater `c`'s raised relief, rim and ejecta, kept at `dx` east and `dy` north of
/// its center: 1 less the removed share times the squared cosine of the angle from upslope.
inline float KernelSlopeRaisedShare(KERNEL_IN(CraterRecord) c, float dx, float dy) {
    const float distance = sqrt(dx * dx + dy * dy);
    if (!(distance > 0.0f)) {
        return 1.0f;
    }
    const float removed = sqrt(c.upslopeX * c.upslopeX + c.upslopeY * c.upslopeY);
    const float cosine = max(0.0f, (c.upslopeX * dx + c.upslopeY * dy) / (removed * distance));
    return 1.0f - removed * cosine * cosine;
}

/// `KernelProfileDistance` at `dx` east and `dy` north of crater `c`'s center after its floor
/// shift (`CraterSlopeShape::shift`).
inline float KernelShiftedDistance(CraterData data, KERNEL_IN(CraterRecord) c, float dx, float dy) {
    const float shift =
        c.shiftX != 0.0f || c.shiftY != 0.0f ? KernelSlopeShiftWeight(c, dx, dy) : 0.0f;
    return KernelProfileDistance(data, c, dx - shift * c.shiftX, dy - shift * c.shiftY);
}

/// Profile height `height` (m) at `dx` east and `dy` north of crater `c`'s center with its raised
/// relief lowered upslope (`CraterSlopeShape::upslope`).
inline float KernelUpslopeHeight(KERNEL_IN(CraterRecord) c, float height, float dx, float dy) {
    if (height > 0.0f && (c.upslopeX != 0.0f || c.upslopeY != 0.0f)) {
        return height * KernelSlopeRaisedShare(c, dx, dy);
    }
    return height;
}

/// `height` after crater `c`, at `dx` east and `dy` north of its center in meters, as
/// `ApplyCraters` shapes a cell there; unchanged beyond its reach.
inline float ApplyCraterKernel(KERNEL_IN(CraterShapeKernel) k, CraterData data,
                               KERNEL_IN(CraterRecord) c, float height, float dx, float dy) {
    const float diameter = c.diameter;
    const float radius = 0.5f * diameter;
    const float distance = KernelShiftedDistance(data, c, dx, dy);
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
    const float bare =
        KernelUpslopeHeight(c, KernelRoundedHeight(k, data, distance, diameter, c), dx, dy);
    const float shaped =
        bare + KernelNoiseHeight(k, c, dx, dy, radii) + KernelTextureHeight(k, c, dx, dy, radii);
    return height + c.resolution * shaped + c.resolution * replaced;
}

/// What fitting crater datums takes: the craters' shapes, the fit footprint and the lattice the
/// ground is sampled on (`FitCraterDatum`), and the size bands that index the craters.
struct CraterDatumKernel {
    CraterShapeKernel shape;
    float fitRadii = 1.0f;      ///< The fit footprint, in crater radii.
    uint32_t latticeSide = 7u;  ///< Lattice points per side.
    uint32_t bandCount = 0u;    ///< Size bands of the crater index.
};

/// The relief (m) crater `c` adds at `dx` east and `dy` north of its center without its datum,
/// as datums are fitted to it: its resolution times the bare profile, within the profile's
/// reach. `bound` (m) is at least that reach stretched by the crater's outline.
inline float CraterRelief(KERNEL_IN(CraterShapeKernel) k, CraterData data,
                          KERNEL_IN(CraterRecord) c, float bound, float dx, float dy) {
    const float squared = dx * dx + dy * dy;
    if (!(squared < bound * bound)) {
        return 0.0f;
    }
    const float distance = KernelShiftedDistance(data, c, dx, dy);
    if (!(distance < k.profileReachRadii * 0.5f * c.diameter)) {
        return 0.0f;
    }
    return c.resolution *
           KernelUpslopeHeight(c, KernelProfileHeight(k, data, distance, c.diameter, c), dx, dy);
}

/// The offset (m) of lattice point `index` of `side` across a footprint of `fitRadius`.
inline float KernelDatumOffset(uint32_t index, uint32_t side, float fitRadius) {
    return fitRadius * ((2.0f * float(index) + 1.0f) / float(side) - 1.0f);
}

/// Relative float error allowed for, in cell lookups, at a position's magnitude.
static const float kDatumCellSlack = 4.8e-7f;

/// The first and last cells of a band's row or column within one cell of position `p`.
struct KernelCellSpan {
    int32_t first;
    int32_t last;
};

/// The cells of side `size`, `count` of them from absolute cell `start`, within one cell plus
/// float slack `slack` of position `p`, clamped to the band.
inline KernelCellSpan KernelCellsNear(float p, float size, float slack, float start,
                                      int32_t count) {
    KernelCellSpan span;
    span.first = max(int32_t(floor((p - size - slack) / size) - start), 0);
    span.last = min(int32_t(floor((p + size + slack) / size) - start), count - 1);
    return span;
}

/// The relief (m) of the craters before crater `k` of `records` at lattice point `point` of `k`'s
/// datum footprint, numbered row by row; 0 outside the footprint. The index is laid out as
/// `DatumKernelIndex` (shaping/datum_fit.cpp) describes, with `lows` the x, y pairs the records'
/// float positions drop.
inline float KernelDatumRelief(KERNEL_IN(CraterDatumKernel) kernel, CraterData data,
                               CraterRecords records, KernelFloats lows, KernelFloats bandFloats,
                               KernelUints bandCells, KernelUints cells, KernelUints entries,
                               uint32_t k, uint32_t point) {
    const uint32_t side = kernel.latticeSide;
    const float fitRadius = kernel.fitRadii * 0.5f * records[k].diameter;
    const float dx = KernelDatumOffset(point % side, side, fitRadius);
    const float dy = KernelDatumOffset(point / side, side, fitRadius);
    if (dx * dx + dy * dy >= fitRadius * fitRadius) {
        return 0.0f;
    }
    const float lowX = lows[2u * k] + dx;
    const float lowY = lows[2u * k + 1u] + dy;
    const float px = records[k].x + lowX;
    const float py = records[k].y + lowY;
    const float slackX = kDatumCellSlack * abs(px);
    const float slackY = kDatumCellSlack * abs(py);
    float relief = 0.0f;
    for (uint32_t b = 0u; b < kernel.bandCount; ++b) {
        const float size = bandFloats[3u * b];
        const int32_t across = int32_t(bandCells[3u * b]);
        const KernelCellSpan columns =
            KernelCellsNear(px, size, slackX, bandFloats[3u * b + 1u], across);
        const KernelCellSpan rows = KernelCellsNear(py, size, slackY, bandFloats[3u * b + 2u],
                                                    int32_t(bandCells[3u * b + 1u]));
        for (int32_t j = rows.first; j <= rows.last; ++j) {
            for (int32_t i = columns.first; i <= columns.last; ++i) {
                const uint32_t cell = bandCells[3u * b + 2u] + uint32_t(j * across + i);
                const uint32_t first = cells[2u * cell];
                const uint32_t end = first + cells[2u * cell + 1u];
                for (uint32_t e = first; e < end; ++e) {
                    const uint32_t n = entries[e];
                    if (n >= k) {
                        break;
                    }
                    relief +=
                        CraterRelief(kernel.shape, data, records[n], size,
                                     (records[k].x - records[n].x) + (lowX - lows[2u * n]),
                                     (records[k].y - records[n].y) + (lowY - lows[2u * n + 1u]));
                }
            }
        }
    }
    return relief;
}

/// What fitting crater datums on a body takes on the GPU: the craters' shapes.
struct CraterBodyDatumKernel {
    CraterShapeKernel shape;
};

/// The relief (m) of craters `entries[first]` to `entries[first + count - 1]` of `records`, in
/// that order, at the point at position (`px`, `py`, `pz`) from the dispatch's origin (m) on a
/// body; each crater read in its `frames` (`KernelFrameOffset`) within its `bounds` (m), as
/// `CraterRelief` takes them.
inline float KernelBodyDatumRelief(KERNEL_IN(CraterShapeKernel) k, CraterData data,
                                   CraterRecords records, CraterFrames frames, KernelFloats bounds,
                                   KernelUints entries, uint32_t first, uint32_t count, float px,
                                   float py, float pz) {
    float relief = 0.0f;
    for (uint32_t e = first; e < first + count; ++e) {
        const uint32_t n = entries[e];
        const KernelOffset at = KernelFrameOffset(frames[n], px, py, pz);
        relief += CraterRelief(k, data, records[n], bounds[n], at.dx, at.dy);
    }
    return relief;
}

#ifndef __SLANG__
static_assert(sizeof(CraterProfileKernel) == 76,
              "CraterProfileKernel must match its std430 layout");
static_assert(sizeof(CraterShapeKernel) == 180, "CraterShapeKernel must match its std430 layout");
static_assert(sizeof(CraterRecord) == 116, "CraterRecord must match its std430 layout");
static_assert(sizeof(CraterFrame) == 40, "CraterFrame must match its std430 layout");
static_assert(sizeof(CraterDatumKernel) == 192, "CraterDatumKernel must match its std430 layout");
static_assert(sizeof(CraterBodyDatumKernel) == 180,
              "CraterBodyDatumKernel must match its std430 layout");

/// The compiled forms of the crater kernel.
template <> KernelProgram CompiledKernel<CraterShapeKernel>();

/// The compiled forms of the crater datum kernel.
template <> KernelProgram CompiledKernel<CraterDatumKernel>();

/// The compiled forms of the crater datum kernel on bodies.
template <> KernelProgram CompiledKernel<CraterBodyDatumKernel>();
#endif

}  // namespace terraingen
