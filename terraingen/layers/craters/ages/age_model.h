/// @file
/// @brief `CraterAgeModel`, which assigns ages to craters by diameter.
#pragma once

#include "terraingen/random/stream.h"

namespace terraingen {

/// Assigns ages to craters by diameter.
///
/// Each model has a visible lifetime per diameter, the span over which craters of that size are
/// found, and draws ages within it. `RemainingLifetime`, 1 - age / lifetime, expresses the same
/// age as the share of the lifetime left, for degradation models that work in it rather than in
/// years.
class CraterAgeModel {
  public:
    virtual ~CraterAgeModel() = default;

    /// @brief The visible lifetime of craters of a size.
    /// @param diameter The craters' diameter, in metres.
    /// @return The lifetime, in Gyr.
    /// @throws std::domain_error if `diameter` is not positive and finite, or the model is not
    /// defined there.
    double LifetimeGyr(double diameter) const;

    /// @brief Draws a crater's age.
    /// @param random The stream to draw from.
    /// @param diameter The crater's diameter, in metres.
    /// @return The age in Gyr, within [0, `LifetimeGyr(diameter)`].
    /// @throws std::domain_error under the same conditions as `LifetimeGyr`.
    double SampleAgeGyr(RandomStream& random, double diameter) const;

    /// @brief The share of a crater's lifetime left at an age.
    /// @param diameter The crater's diameter, in metres.
    /// @param ageGyr Its age, in Gyr.
    /// @return 1 - `ageGyr` / `LifetimeGyr(diameter)`, clamped to [0, 1]; 1 when the lifetime is
    /// zero.
    /// @throws std::domain_error under the same conditions as `LifetimeGyr`.
    double RemainingLifetime(double diameter, double ageGyr) const;

  protected:
    /// Lifetime for a valid diameter.
    virtual double LifetimeAt(double diameter) const = 0;

    /// Age for a valid diameter with lifetime `lifetimeGyr`.
    virtual double SampleAgeAt(RandomStream& random, double diameter, double lifetimeGyr) const = 0;
};

}  // namespace terraingen
