/// @file
/// @brief `CraterProfile`, a crater's height as a function of distance from its centre.
#pragma once

#include "terraingen/layers/craters/degradation/state.h"

namespace terraingen {

/// @brief The topography of a crater: height relative to the pre-impact surface by distance from
/// the centre, for a degradation state.
class CraterProfile {
  public:
    virtual ~CraterProfile() = default;

    /// @brief The height of a crater at a distance from its centre.
    /// @param distance Distance from the centre, in metres.
    /// @param diameter Rim-to-rim diameter, in metres.
    /// @param state Degradation state.
    /// @return Height relative to the surface the crater formed on, in metres.
    virtual double Height(double distance, double diameter, const CraterState& state) const = 0;

    /// @brief Distance beyond which `Height` is zero.
    /// @return The reach, in crater radii.
    virtual double ReachRadii() const = 0;

    /// @brief Whether a crater has the complex form: flat floor, terraced walls, central peak.
    /// @param diameter The crater's diameter, in metres.
    /// @return True for a complex crater; false by default.
    virtual bool IsComplex(double /*diameter*/) const {
        return false;
    }
};

}  // namespace terraingen
