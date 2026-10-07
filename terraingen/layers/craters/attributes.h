#pragma once

#include "terraingen/layers/craters/degradation/state.h"
#include "terraingen/layers/craters/population.h"
#include "terraingen/layers/craters/profiles/profile.h"

namespace terraingen {

/// What a crater looks like in its current state, for programs that list, label or place craters.
struct CraterAttributes {
    double floorDepth = 0.0;  ///< Depth of the floor below the surrounding surface (m).
    double rimHeight = 0.0;   ///< Height of the rim crest above the surrounding surface (m).
    double depthRatio = 0.0;  ///< Rim-to-floor depth over diameter now.
    /// The depth ratio now over that when fresh: 1 for a fresh crater, falling as it degrades.
    double freshness = 1.0;
    bool complex = false;  ///< Of the complex form: a flat floor and a central peak.

    bool operator==(const CraterAttributes&) const = default;
};

/// The attributes of `crater` in `state` shaped by `profile`: the floor as the profile's lowest
/// point inside the rim and the rim crest as its highest from 0.7 to 1.3 radii (both sampled every
/// 0.01 radii), the ratios from `state`, and the form from `profile`.
CraterAttributes ComputeCraterAttributes(const Crater& crater, const CraterState& state,
                                         const CraterProfile& profile);

}  // namespace terraingen
