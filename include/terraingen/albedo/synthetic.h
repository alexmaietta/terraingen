// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Synthetic albedos: a base color with noise in hue, saturation and value.
#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/geometry/geometry.h"
#include "terraingen/geometry/surface.h"
#include "terraingen/noise/fractals.h"

#include <vector>

namespace terraingen {

/// The default value of a synthetic albedo: the reflectance of mature mare soil at 750 nm.
inline constexpr double kDefaultSyntheticAlbedoValue = 0.110;

/// The color of a `SyntheticAlbedo`: a base color in hue, saturation and value, each
/// varied by a stack of fractals (`EvaluateFractals`, with amplitudes in the channel's
/// units).
struct SyntheticAlbedoOptions {
    /// Three bands (red, green, blue) from the color; otherwise one band, its value.
    bool rgb = false;
    double hueDegrees = 0.0;  ///< Degrees; noise added to it wraps around.
    double saturation = 0.0;  ///< In [0, 1]; 0 is gray.
    /// In [0, 1]: the brightest band's reflectance (`kDefaultSyntheticAlbedoValue` by default).
    double value = kDefaultSyntheticAlbedoValue;
    std::vector<Fractal> hueNoiseDegrees;  ///< Added to the hue, in degrees.
    std::vector<Fractal> saturationNoise;  ///< Added to the saturation.
    /// Relative: the value is multiplied by 1 + the noise, so its contrast scales with it.
    std::vector<Fractal> valueNoise;

    bool operator==(const SyntheticAlbedoOptions& other) const {
        return rgb == other.rgb && hueDegrees == other.hueDegrees &&
               saturation == other.saturation && value == other.value &&
               hueNoiseDegrees == other.hueNoiseDegrees &&
               saturationNoise == other.saturationNoise && valueNoise == other.valueNoise;
    }
    bool operator!=(const SyntheticAlbedoOptions& other) const {
        return !(*this == other);
    }
};

/// Checks synthetic albedo settings.
/// @throws std::invalid_argument for a hue, saturation or value that is not finite, a saturation
/// or value outside [0, 1], more than `kMaxFractals` fractals in a stack, or an invalid fractal.
void ValidateSyntheticAlbedoOptions(const SyntheticAlbedoOptions& options);

/// Converts a color from hue, saturation and value to red, green and blue in [0, 1] by the
/// hexcone model (Smith 1978).
/// @param hueDegrees Hue in degrees; any value, wrapped.
/// @param saturation Saturation in [0, 1].
/// @param value Value in [0, 1].
AlbedoSample HsvToRgb(double hueDegrees, double saturation, double value);

/// An albedo for terrain without data: a base color with noise in each channel, the
/// saturation and value clamped to [0, 1] after it. Noise is measured from the context's
/// `ReferencePoint`, keeping single-precision noise accurate where coordinates are large; on a
/// body it is 3D noise at each point's 3D position (`EvaluateFractals3`).
class SyntheticAlbedo final : public AlbedoSource {
  public:
    /// The synthetic albedo with the color and noise of `options`.
    /// @throws std::invalid_argument as `ValidateSyntheticAlbedoOptions`.
    explicit SyntheticAlbedo(SyntheticAlbedoOptions options);

    /// 3 bands when `rgb` is set, otherwise 1.
    std::size_t Bands() const override {
        return options_.rgb ? kMaxAlbedoBands : 1;
    }

    /// The color with noise at world point (`x`, `y`).
    AlbedoSample ValueAt(double x, double y, const LayerContext& context) const override;

    /// The color and its noise.
    const SyntheticAlbedoOptions& Options() const {
        return options_;
    }

  private:
    static double Noise(const std::vector<Fractal>& fractals, double x, double y,
                        const LayerContext& context);

    SyntheticAlbedoOptions options_;
};

}  // namespace terraingen
