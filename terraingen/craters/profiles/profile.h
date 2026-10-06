#pragma once

#include "terraingen/craters/degradation/state.h"

namespace terraingen {

/// The topography of a crater: height relative to the pre-impact surface as a function of distance
/// from the centre, for a given degradation state.
class CraterProfile {
  public:
    virtual ~CraterProfile() = default;

    /// Height in metres, relative to the surface the crater formed on, at `distance` metres from
    /// the centre of a crater `diameter` metres across (rim to rim) in degradation state `state`.
    virtual double Height(double distance, double diameter, const CraterState& state) const = 0;

    /// Distance from the centre, in crater radii, beyond which `Height` is zero.
    virtual double ReachRadii() const = 0;
};

}  // namespace terraingen
