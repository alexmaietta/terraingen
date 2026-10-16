/// @file
/// @brief `CraterProfile`, a crater's height as a function of distance from its centre.
#pragma once

#include "terraingen/layers/craters/degradation/state.h"

namespace terraingen {

/// The topography of a crater: height relative to the pre-impact surface by distance from
/// the centre, for a degradation state.
class CraterProfile {
  public:
    virtual ~CraterProfile() = default;

    /// The height (m) relative to the surface the crater formed on, at `distance` metres from the
    /// centre of a crater `diameter` metres rim to rim.
    virtual double Height(double distance, double diameter, const CraterState& state) const = 0;

    /// The distance in crater radii beyond which `Height` is zero.
    virtual double ReachRadii() const = 0;

    /// Whether a crater of `diameter` metres has the complex form (flat floor, terraced walls,
    /// central peak); false by default.
    virtual bool IsComplex(double /*diameter*/) const {
        return false;
    }
};

}  // namespace terraingen
