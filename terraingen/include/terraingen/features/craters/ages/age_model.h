#pragma once

#include "terraingen/random/stream.h"

namespace terraingen {

/// Assigns ages to craters by diameter.
///
/// Each model has a visible lifetime per diameter, the span over which craters of that size are
/// found, and draws ages within it. Freshness, 1 - age / lifetime, expresses the same age as the
/// fraction of the lifetime remaining, for degradation models that work in freshness rather than
/// years.
class CraterAgeModel {
  public:
    virtual ~CraterAgeModel() = default;

    /// Visible lifetime of craters `diameter` metres across, in Gyr.
    /// @throws std::domain_error if `diameter` is not positive and finite, or the model is not
    /// defined there.
    double LifetimeGyr(double diameter) const;

    /// Age in Gyr, within [0, `LifetimeGyr(diameter)`], of a crater `diameter` metres across.
    /// @throws std::domain_error under the same conditions as `LifetimeGyr`.
    double SampleAgeGyr(RandomStream& random, double diameter) const;

    /// 1 - `ageGyr` / `LifetimeGyr(diameter)`, clamped to [0, 1]; 1 when the lifetime is zero.
    /// @throws std::domain_error under the same conditions as `LifetimeGyr`.
    double Freshness(double diameter, double ageGyr) const;

  protected:
    /// Lifetime for a valid diameter.
    virtual double LifetimeAt(double diameter) const = 0;

    /// Age for a valid diameter with lifetime `lifetimeGyr`.
    virtual double SampleAgeAt(RandomStream& random, double diameter, double lifetimeGyr) const = 0;
};

}  // namespace terraingen
