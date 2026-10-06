#pragma once

#include "terraingen/layers/craters/degradation/state.h"
#include "terraingen/layers/craters/population.h"
#include "terraingen/layers/craters/profiles/profile.h"

namespace terraingen {

/// What a crater looks like in its current state, for programs that list, label or place craters.
struct CraterAttributes {
    double floorDepth = 0.0;  ///< Depth of the centre below the surrounding surface (m).
    double rimHeight = 0.0;   ///< Height of the rim crest above the surrounding surface (m).
    double depthRatio = 0.0;  ///< Rim-to-floor depth over diameter now.
    /// The depth ratio now over that when fresh: 1 for a fresh crater, falling as it degrades.
    double freshness = 1.0;

    bool operator==(const CraterAttributes&) const = default;
};

/// The attributes of `crater` in `state` shaped by `profile`: the floor from the profile at the
/// centre, the rim crest as the profile's highest point from 0.7 to 1.3 radii (sampled every 0.01
/// radii), and the ratios from `state`.
CraterAttributes ComputeCraterAttributes(const Crater& crater, const CraterState& state,
                                         const CraterProfile& profile);

}  // namespace terraingen
