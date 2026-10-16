/// @file
/// @brief The simple-crater profile of Martin, Parkes and Dunstan (2014).
#pragma once

#include "terraingen/layers/craters/profiles/profile.h"

namespace terraingen {

/// @brief Simple-crater profile of Martin, Parkes and Dunstan (2014): bowl, inner rim, outer rim
/// and ejecta polynomials joined continuously; degradation rounds the rim.
class MartinParkesDunstanProfile final : public CraterProfile {
  public:
    /// @brief The height of a crater at a distance from its centre.
    /// @param distance Distance from the centre, in metres.
    /// @param diameter Rim-to-rim diameter, in metres.
    /// @param state Degradation state; fresh and current rim heights and current depth are read.
    /// @return Height relative to the pre-impact surface, in metres; 0 from two radii.
    /// @throws std::invalid_argument if the state's depth is not positive or its current rim height
    /// is negative or exceeds the fresh rim height.
    double Height(double distance, double diameter, const CraterState& state) const override;

    /// @brief Distance beyond which the height is zero.
    /// @return 2 crater radii.
    double ReachRadii() const override {
        return 2.0;
    }
};

}  // namespace terraingen
