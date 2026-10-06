#pragma once

#include "terraingen/layers/craters/ages/age_model.h"

namespace terraingen {

/// Every crater is fresh: age and lifetime zero.
class FreshAges final : public CraterAgeModel {
  protected:
    double LifetimeAt(double diameter) const override;
    double SampleAgeAt(RandomStream& random, double diameter, double lifetimeGyr) const override;
};

}  // namespace terraingen
