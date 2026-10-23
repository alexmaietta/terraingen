/// @file
/// @brief `CraterAgeModel`, crater ages by diameter, and the simplest: `FreshAges`, `UniformAges`.
#pragma once

#include "terraingen/random/stream.h"

namespace terraingen {

/// Assigns ages to craters by diameter, within a visible lifetime per diameter.
class CraterAgeModel {
  public:
    virtual ~CraterAgeModel() = default;

    /// The visible lifetime in Gyr of craters of `diameter` meters.
    /// @throws std::domain_error if `diameter` is not positive and finite, or the model is not
    /// defined there.
    double LifetimeGyr(double diameter) const;

    /// Draws the age in Gyr, within [0, `LifetimeGyr(diameter)`], of a crater of `diameter` meters.
    /// @throws std::domain_error as `LifetimeGyr`.
    double SampleAgeGyr(RandomStream& random, double diameter) const;

    /// The share of a crater's lifetime left at `ageGyr`: 1 - `ageGyr` / `LifetimeGyr(diameter)`,
    /// clamped to [0, 1]; 1 when the lifetime is zero. Diameter in meters.
    /// @throws std::domain_error as `LifetimeGyr`.
    double RemainingLifetime(double diameter, double ageGyr) const;

  protected:
    /// The lifetime in Gyr for a diameter (m) already checked positive and finite.
    virtual double LifetimeAt(double diameter) const = 0;

    /// Draws an age in Gyr for a diameter in meters; the caller clamps it to [0, `lifetimeGyr`].
    virtual double SampleAgeAt(RandomStream& random, double diameter, double lifetimeGyr) const = 0;
};

/// Every crater is fresh: age and lifetime zero.
class FreshAges final : public CraterAgeModel {
  protected:
    double LifetimeAt(double diameter) const override;
    double SampleAgeAt(RandomStream& random, double diameter, double lifetimeGyr) const override;
};

/// Ages uniform over one lifetime for every diameter, so freshness is uniform in [0, 1].
class UniformAges final : public CraterAgeModel {
  public:
    /// @param lifetimeGyr The lifetime of every crater, in Gyr.
    /// @throws std::invalid_argument unless `lifetimeGyr` is positive and finite.
    explicit UniformAges(double lifetimeGyr);

  protected:
    double LifetimeAt(double diameter) const override;
    double SampleAgeAt(RandomStream& random, double diameter, double lifetimeGyr) const override;

  private:
    double lifetimeGyr_;
};

}  // namespace terraingen
