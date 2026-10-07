#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/noise/kernels/layers.h"
#include "terraingen/util/geometry.h"

#include <vector>

namespace terraingen {

/// Settings for a `SyntheticAlbedo`: a base colour in hue, saturation and value, each varied by a
/// stack of noise layers (`EvaluateNoiseLayers`, as for heights, with amplitudes in the channel's
/// units).
struct SyntheticAlbedoParams {
    /// Three bands (red, green, blue) from the colour; otherwise one band, its value.
    bool rgb = false;
    double hue = 0.0;         ///< Degrees; noise added to it wraps around.
    double saturation = 0.0;  ///< In [0, 1]; 0 is grey.
    /// In [0, 1]: the brightest band's reflectance. The default, 0.110, is the 750 nm reflectance
    /// of mature Apollo 12 mare soil 12070 (Lucey et al. 2000, Table 1); their highland soils
    /// range from about 0.19 to 0.41.
    double value = 0.110;
    std::vector<NoiseLayer> hueNoise;         ///< Added to the hue, in degrees.
    std::vector<NoiseLayer> saturationNoise;  ///< Added to the saturation.
    /// Relative: the value is multiplied by 1 + the noise, so its contrast scales with it.
    std::vector<NoiseLayer> valueNoise;

    bool operator==(const SyntheticAlbedoParams&) const = default;
};

/// @throws std::invalid_argument for a hue, saturation or value that is not finite, a saturation
/// or value outside [0, 1], more than `kMaxNoiseLayers` layers in a stack, or an invalid layer.
void ValidateSyntheticAlbedo(const SyntheticAlbedoParams& params);

/// Red, green and blue in [0, 1] of the colour with `hue` in degrees (any, wrapped), and
/// `saturation` and `value` in [0, 1], by the hexcone model.
///
/// Reference: A. R. Smith, "Color gamut transform pairs", SIGGRAPH '78, Computer Graphics
/// 12(3):12-19, 1978. https://doi.org/10.1145/965139.807361
AlbedoSample HsvToRgb(double hue, double saturation, double value);

/// An albedo for terrain without data: a base colour with noise in each channel, the saturation
/// and value clamped to [0, 1] after it. Noise is measured from `origin`, so single-precision noise
/// stays accurate in projections whose coordinates are large.
class SyntheticAlbedo final : public AlbedoSource {
  public:
    /// @throws std::invalid_argument as `ValidateSyntheticAlbedo`.
    explicit SyntheticAlbedo(SyntheticAlbedoParams params, Vec2d origin = {});

    std::size_t Bands() const override {
        return params_.rgb ? kMaxAlbedoBands : 1;
    }

    AlbedoSample ValueAt(double x, double y, const AlbedoContext& context) const override;

    const SyntheticAlbedoParams& Params() const {
        return params_;
    }

  private:
    SyntheticAlbedoParams params_;
    Vec2d origin_;
};

}  // namespace terraingen
