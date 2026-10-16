/// @file
/// @brief Synthetic albedos: a base colour with noise in hue, saturation and value.
#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/noise/noise_layers.h"
#include "terraingen/util/geometry.h"

#include <vector>

namespace terraingen {

/// The colour of a `SyntheticAlbedo`: a base colour in hue, saturation and value, each
/// varied by a stack of noise layers (`EvaluateNoiseLayers`, with amplitudes in the channel's
/// units).
struct SyntheticAlbedoParams {
    /// Three bands (red, green, blue) from the colour; otherwise one band, its value.
    bool rgb = false;
    double hue = 0.0;         ///< Degrees; noise added to it wraps around.
    double saturation = 0.0;  ///< In [0, 1]; 0 is grey.
    /// In [0, 1]: the brightest band's reflectance; the default is mature mare soil at 750 nm.
    double value = 0.110;
    std::vector<NoiseLayer> hueNoise;         ///< Added to the hue, in degrees.
    std::vector<NoiseLayer> saturationNoise;  ///< Added to the saturation.
    /// Relative: the value is multiplied by 1 + the noise, so its contrast scales with it.
    std::vector<NoiseLayer> valueNoise;

    bool operator==(const SyntheticAlbedoParams&) const = default;
};

/// Checks synthetic albedo settings.
/// @throws std::invalid_argument for a hue, saturation or value that is not finite, a saturation
/// or value outside [0, 1], more than `kMaxNoiseLayers` layers in a stack, or an invalid layer.
void ValidateSyntheticAlbedo(const SyntheticAlbedoParams& params);

/// Converts a colour from hue, saturation and value to red, green and blue in [0, 1] by the
/// hexcone model (Smith 1978).
/// @param hue Hue in degrees; any value, wrapped.
/// @param saturation Saturation in [0, 1].
/// @param value Value in [0, 1].
AlbedoSample HsvToRgb(double hue, double saturation, double value);

/// An albedo for terrain without data: a base colour with noise in each channel, the
/// saturation and value clamped to [0, 1] after it. Noise is measured from `origin`; on a sphere it
/// is 3D noise at each point's 3D position (`EvaluateNoiseLayers3`).
class SyntheticAlbedo final : public AlbedoSource {
  public:
    /// The synthetic albedo with the colour and noise of `params`.
    /// @param origin Point noise is measured from, keeping single-precision noise accurate where
    /// coordinates are large.
    /// @throws std::invalid_argument as `ValidateSyntheticAlbedo`, or on a sphere for a noise
    /// basis without a 3D form.
    explicit SyntheticAlbedo(SyntheticAlbedoParams params, Vec2d origin = {}, Surface surface = {});

    /// 3 bands when `rgb` is set, otherwise 1.
    std::size_t Bands() const override {
        return params_.rgb ? kMaxAlbedoBands : 1;
    }

    /// The colour with noise at world point (`x`, `y`).
    AlbedoSample ValueAt(double x, double y, const AlbedoContext& context) const override;

    /// The colour and its noise.
    const SyntheticAlbedoParams& Params() const {
        return params_;
    }

  private:
    double Noise(const std::vector<NoiseLayer>& layers, double x, double y) const;

    SyntheticAlbedoParams params_;
    Vec2d origin_;
    Surface surface_;
};

}  // namespace terraingen
