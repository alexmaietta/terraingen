/// @file
/// @brief `CraterAgeModel`, which assigns ages to craters by diameter, and the simplest models:
/// `FreshAges` and `UniformAges`.
#pragma once

#include "terraingen/random/stream.h"

namespace terraingen {

/// @brief Assigns ages to craters by diameter, within a visible lifetime per diameter.
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
    /// @brief The lifetime for a diameter already checked positive and finite.
    /// @param diameter The craters' diameter, in metres.
    /// @return The lifetime, in Gyr.
    virtual double LifetimeAt(double diameter) const = 0;

    /// @brief Draws an age; the caller clamps it to [0, `lifetimeGyr`].
    /// @param random The stream to draw from.
    /// @param diameter The crater's diameter, in metres.
    /// @param lifetimeGyr Its lifetime, in Gyr.
    /// @return The age, in Gyr.
    virtual double SampleAgeAt(RandomStream& random, double diameter, double lifetimeGyr) const = 0;
};

/// @brief Every crater is fresh: age and lifetime zero.
class FreshAges final : public CraterAgeModel {
  protected:
    double LifetimeAt(double diameter) const override;
    double SampleAgeAt(RandomStream& random, double diameter, double lifetimeGyr) const override;
};

/// @brief Ages uniform over one lifetime for every diameter, so freshness is uniform in [0, 1].
class UniformAges final : public CraterAgeModel {
  public:
    /// @brief Makes the model.
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
