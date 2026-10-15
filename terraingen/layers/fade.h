/// @file
/// @brief Falloff curves, and the fade of features too small to resolve at a sample spacing.
#pragma once

namespace terraingen {

/// Shape of a falloff from 1 to 0. See SCIENCE.md, "Fades and blends".
enum class BlendShape {
    Linear,        ///< Straight ramp; continuous, with kinks at both ends.
    Smoothstep,    ///< Cubic 3t^2 - 2t^3; continuous first derivative.
    Smootherstep,  ///< Quintic 6t^5 - 15t^4 + 10t^3; continuous first and second derivatives.
    Gaussian,      ///< Gaussian falloff with sigma a third of the band, rescaled to end at zero.
};

/// @brief The cubic smoothstep 3t^2 - 2t^3.
/// @param t Position along the step; clamped to [0, 1].
/// @return 0 at or below 0, 1 at or above 1, and the cubic between.
double Smoothstep(double t);

/// @brief The weight of a falloff from 1 to 0 along `shape`.
/// @param x Where the weight is read, in the units of `start` and `end`.
/// @param start Where the falloff starts: the weight is 1 at or before it.
/// @param end Where it ends: the weight is 0 at or beyond it; above `start`.
/// @param shape The curve in between.
/// @return The weight, in [0, 1].
double Falloff(double x, double start, double end, BlendShape shape);

/// How features too small to resolve at the sample spacing fade out, by how many samples span
/// their diameter: absent up to `startSamples`, whole from `endSamples`, and `shape` in between.
///
/// Fading rather than cutting keeps levels of detail consistent: a crater sampled at a coarse and
/// at a fine spacing changes continuously instead of appearing or vanishing at one spacing.
struct ResolutionFade {
    double startSamples = 2.0;
    double endSamples = 4.0;
    BlendShape shape = BlendShape::Smoothstep;

    bool operator==(const ResolutionFade&) const = default;
};

/// @brief Checks a resolution fade.
/// @param fade The fade to check.
/// @throws std::invalid_argument unless 0 <= `startSamples` < `endSamples`, both finite.
void ValidateResolutionFade(const ResolutionFade& fade);

/// @brief The weight of a feature at a sample spacing.
/// @param diameter The feature's size, in the units of `spacing`.
/// @param spacing The sample spacing; positive.
/// @param fade How the feature fades with the samples across it.
/// @return 0 up to `fade.startSamples` samples across the feature, 1 from `fade.endSamples`, and
/// `fade.shape` in between.
double ResolutionWeight(double diameter, double spacing, const ResolutionFade& fade);

}  // namespace terraingen
