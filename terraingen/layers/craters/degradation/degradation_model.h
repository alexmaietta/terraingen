/// @file
/// @brief `DegradationModel`, which maps craters to their degradation state.
#pragma once

#include "terraingen/layers/craters/degradation/state.h"
#include "terraingen/layers/craters/population.h"

namespace terraingen {

/// @brief Maps each crater to its degradation state, from its diameter and age.
class DegradationModel {
  public:
    virtual ~DegradationModel() = default;

    /// @brief The degradation state of a crater.
    /// @param crater The crater; its diameter and age are read.
    /// @return Its fresh and current morphometry.
    /// @throws std::domain_error if the crater's diameter is not positive and finite, or the model
    /// is not defined for it.
    virtual CraterState State(const Crater& crater) const = 0;
};

}  // namespace terraingen
