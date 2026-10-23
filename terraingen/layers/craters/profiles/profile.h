// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `CraterProfile`, a crater's height as a function of distance from its center.
#pragma once

#include "terraingen/layers/craters/degradation/state.h"
#include "terraingen/layers/kernels/craters.h"

namespace terraingen {

/// The topography of a crater: height relative to the pre-impact surface by distance from
/// the center, for a degradation state.
class CraterProfile {
  public:
    virtual ~CraterProfile() = default;

    /// The height (m) relative to the surface the crater formed on, at `distance` meters from the
    /// center of a crater `diameter` meters rim to rim.
    virtual double Height(double distance, double diameter, const CraterState& state) const = 0;

    /// The distance in crater radii beyond which `Height` is zero.
    virtual double ReachRadii() const = 0;

    /// Whether a crater of `diameter` meters has the complex form (flat floor, terraced walls,
    /// central peak); false by default.
    virtual bool IsComplex(double /*diameter*/) const {
        return false;
    }
};

/// The kernel record of a crater of `diameter` meters in `state`, the shape fields only, as the
/// profile kernels read it.
inline CraterRecord KernelStateRecord(double diameter, const CraterState& state) {
    CraterRecord r;
    r.diameter = static_cast<float>(diameter);
    r.freshDepth = static_cast<float>(state.fresh.depthRatio);
    r.freshRim = static_cast<float>(state.fresh.rimHeightRatio);
    r.currentDepth = static_cast<float>(state.current.depthRatio);
    r.currentRim = static_cast<float>(state.current.rimHeightRatio);
    r.diffusion = static_cast<float>(state.diffusion);
    return r;
}

}  // namespace terraingen
