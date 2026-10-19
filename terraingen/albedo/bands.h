/// @file
/// @brief Internal helpers of the albedo layers: band scaling, soft edges, lobes and lifetimes.
#pragma once

#include "terraingen/albedo/albedo.h"

#include <cstddef>
#include <cstdint>
#include <numbers>

namespace terraingen {

/// Ring frequency of edge lobes about 45 degrees wide, for `RingNoise`.
inline constexpr double kLobeRing = 180.0 / (std::numbers::pi * 45.0);

/// Multiplies the first `bands` values of `value` by `factor`; NaN stays NaN.
void ScaleBands(AlbedoSample& value, std::size_t bands, double factor);

/// Changes the first `bands` values of `value` by the relative reflectance `change`, never below
/// 0; NaN stays NaN and a change of 0 leaves them alone.
void ChangeBands(AlbedoSample& value, std::size_t bands, double change);

/// Moves the first `bands` values of `value` from optical maturity `reference` to `maturity`
/// (`ReflectanceAtMaturity`), then multiplies them by `factor`; NaN stays NaN.
void MatureBands(AlbedoSample& value, std::size_t bands, double reference, double maturity,
                 double originReflectance, double factor = 1.0);

/// A smoothstep from 0 at `start` to 1 at `start` + `width`.
double SmoothRamp(double t, double start, double width);

/// A smoothstep from 1 at `start` to 0 at `start` + `width`: 1 - `SmoothRamp`.
double FadeOut(double t, double start, double width);

/// An edge at `t` = 1: 1 inside, fading to 0 over the last `feather` of the way out.
double SoftEdge(double t, double feather);

/// Fractal noise of unit wavelength and amplitude with `octaves` octaves at (`ring` cos `angle`,
/// `ring` sin `angle`, `z`): noise around a circle, `ring` / (2 pi) features per radian.
double RingNoise(double ring, double angle, double z, std::uint32_t seed, int octaves);

/// The share of its radius a lobed edge keeps where `RingNoise` reads `noise`: 1 less
/// `raggedness` times `noise` below 0.5, clamped to [0, 1].
double LobeFactor(double noise, double raggedness);

/// The share of its life left to something `ageGyr` old that lasts `lifetimeMyr`: 1 when new, 0
/// at the end and negative after.
double LifeLeft(double ageGyr, double lifetimeMyr);

}  // namespace terraingen
