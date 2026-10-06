#pragma once

#include "terraingen/craters/degradation/state.h"
#include "terraingen/craters/population.h"

namespace terraingen {

/// Maps each crater to its degradation state, from its diameter and age.
class DegradationModel {
  public:
    virtual ~DegradationModel() = default;

    /// @throws std::domain_error if the crater's diameter is not positive and finite, or the model
    /// is not defined for it.
    virtual CraterState State(const Crater& crater) const = 0;
};

}  // namespace terraingen
