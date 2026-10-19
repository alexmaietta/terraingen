/// @file
/// @brief `FreshnessAges`, crater ages from a distribution of freshness.
#pragma once

#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/random/value_distribution.h"

namespace terraingen {

/// Ages from a freshness f in [0, 1] drawn per crater: age = (1 - f) * lifetime, with one lifetime
/// for every diameter, so `RemainingLifetime` gives back f.
class FreshnessAges final : public CraterAgeModel {
  public:
    /// @param freshness The distribution f is drawn from; its clamp is narrowed to [0, 1].
    /// @param lifetimeGyr The lifetime of every crater, in Gyr.
    /// @throws std::invalid_argument unless `lifetimeGyr` is positive and finite and `freshness`,
    /// once clamped to [0, 1], is valid (`ValidateValueDistribution`).
    FreshnessAges(ValueDistribution freshness, double lifetimeGyr);

    /// The distribution freshness is drawn from, clamped to [0, 1].
    const ValueDistribution& Freshness() const {
        return freshness_;
    }

    /// Draws a freshness in [0, 1] from two draws of `random`.
    double SampleFreshness(RandomStream& random) const;

  protected:
    double LifetimeAt(double diameter) const override;
    double SampleAgeAt(RandomStream& random, double diameter, double lifetimeGyr) const override;

  private:
    ValueDistribution freshness_;
    double lifetimeGyr_;
};

}  // namespace terraingen
