/// @file
/// @brief Falloff curves, and the fade of features too small to resolve at a sample spacing.
#pragma once

namespace terraingen {

/// Shape of a falloff from 1 to 0.
enum class BlendShape {
    Linear,        ///< Straight ramp; continuous, with kinks at both ends.
    Smoothstep,    ///< Cubic 3t^2 - 2t^3; continuous first derivative.
    Smootherstep,  ///< Quintic 6t^5 - 15t^4 + 10t^3; continuous first and second derivatives.
    Gaussian,      ///< Gaussian falloff with sigma a third of the band, rescaled to end at zero.
};

/// The cubic smoothstep 3t^2 - 2t^3, with `t` clamped to [0, 1].
double Smoothstep(double t);

/// The weight in [0, 1] of a falloff from 1 at or before `start` to 0 at or beyond `end`.
/// @param end Above `start`.
double Falloff(double x, double start, double end, BlendShape shape);

/// How features too small to resolve at the sample spacing fade out, by the samples across
/// their diameter.
struct ResolutionFade {
    double startSamples = 2.0;  ///< Samples across at or below which the feature is absent.
    double endSamples = 4.0;    ///< Samples across from which it is whole; above `startSamples`.
    BlendShape shape = BlendShape::Smoothstep;  ///< The curve in between.

    bool operator==(const ResolutionFade& other) const {
        return startSamples == other.startSamples && endSamples == other.endSamples &&
               shape == other.shape;
    }
    bool operator!=(const ResolutionFade& other) const {
        return !(*this == other);
    }
};

/// Checks a resolution fade.
/// @throws std::invalid_argument unless 0 <= `startSamples` < `endSamples`, both finite.
void ValidateResolutionFade(const ResolutionFade& fade);

/// The weight of a feature at a sample spacing: 0 up to `fade.startSamples` samples across it, 1
/// from `fade.endSamples`, and `fade.shape` in between.
/// @param diameter The feature's size, in the units of `spacing`.
/// @param spacing Positive.
double ResolutionWeight(double diameter, double spacing, const ResolutionFade& fade);

}  // namespace terraingen
