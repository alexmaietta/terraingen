/// @file
/// @brief The simple-crater profile of Martin, Parkes and Dunstan (2014).
#pragma once

#include "terraingen/layers/craters/profiles/profile.h"

namespace terraingen {

/// Simple-crater profile of Martin, Parkes and Dunstan (2014), developed for the PANGU planetary
/// surface simulator: four connected polynomials in x = 2 r / D - 1 (-1 at the centre, 0 at the
/// rim, 1 at two radii) for the bowl, inner rim, outer rim and ejecta. Degradation rounds the rim,
/// moving the inner and outer rim joins apart. The profile is relative to the surface the crater
/// formed on, since `ApplyCraters` places it on a datum; it departs from the published equations
/// where they are discontinuous. See SCIENCE.md, "Martin, Parkes and Dunstan profile".
class MartinParkesDunstanProfile final : public CraterProfile {
  public:
    /// @throws std::invalid_argument if the state's depth is not positive or its current rim height
    /// is negative or exceeds the fresh rim height.
    double Height(double distance, double diameter, const CraterState& state) const override;

    double ReachRadii() const override {
        return 2.0;
    }
};

}  // namespace terraingen
