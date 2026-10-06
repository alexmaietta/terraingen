#pragma once

#include "terraingen/layer/craters/ages/age_model.h"

namespace terraingen {

/// Ages uniform over one lifetime for every diameter, so freshness is uniform in [0, 1].
class UniformAges final : public CraterAgeModel {
  public:
    /// @throws std::invalid_argument unless `lifetimeGyr` is positive and finite.
    explicit UniformAges(double lifetimeGyr);

  protected:
    double LifetimeAt(double diameter) const override;
    double SampleAgeAt(RandomStream& random, double diameter, double lifetimeGyr) const override;

  private:
    double lifetimeGyr_;
};

}  // namespace terraingen
