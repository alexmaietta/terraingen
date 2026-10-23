/// @file
/// @brief Internal helpers of the albedo layers: band scaling, soft edges, lobes and lifetimes.
#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/random/value_distribution.h"
#include "terraingen/util/geometry.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace terraingen {

/// Ring frequency of edge lobes about 45 degrees wide, for `RingNoise`.
inline constexpr double kLobeRing = kRadian / 45.0;

/// Multiplies the first `bands` values of `value` by `factor`; NaN stays NaN.
void ScaleBands(AlbedoSample& value, std::size_t bands, double factor);

/// Changes the first `bands` values of `value` by the relative reflectance `change`, never below
/// 0; NaN stays NaN and a change of 0 leaves them alone.
void ChangeBands(AlbedoSample& value, std::size_t bands, double change);

/// Relative reflectance changes laid over one another, the first on top: each covers its share of
/// what the ones before it leave showing.
class ChangeOverlay {
  public:
    /// Lays down a layer covering `cover` (in [0, 1]) of what shows, changing it by `change` there.
    void Add(double cover, double change) {
        change_ += open_ * cover * change;
        open_ *= 1.0 - cover;
    }

    /// The summed relative change.
    double Change() const {
        return change_;
    }

  private:
    double change_ = 0.0;
    double open_ = 1.0;
};

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

/// The share of its radius a lobed edge keeps at `angle` radians: 1 less `raggedness` times how
/// far `RingNoise(ring, angle, z, seed, octaves)` reads below 0.5; 1 without raggedness.
double EdgeLobe(double raggedness, double ring, double angle, double z, std::uint32_t seed,
                int octaves);

/// Checks a distribution of relative reflectance changes.
/// @param what The change, for messages, such as "A dark splotch's change".
/// @throws std::invalid_argument for an invalid distribution, or one that can reach -1 or is
/// unbounded.
void ValidateRelativeChange(const ValueDistribution& change, const std::string& what);

/// The share of its life left to something `ageGyr` old that lasts `lifetimeMyr`: 1 when new, 0
/// at the end and negative after.
double LifeLeft(double ageGyr, double lifetimeMyr);

}  // namespace terraingen
