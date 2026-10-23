/// @file
/// @brief `CraterAttributes`, what a crater looks like in its current state.
#pragma once

#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/craters/degradation/state.h"
#include "terraingen/layers/craters/profiles/profile.h"

namespace terraingen {

/// What a crater looks like in its current state, for listing, labeling or placing.
struct CraterAttributes {
    double floorDepth = 0.0;  ///< Depth of the floor below the surrounding surface (m).
    double rimHeight = 0.0;   ///< Height of the rim crest above the surrounding surface (m).
    double depthRatio = 0.0;  ///< Rim-to-floor depth over diameter now.
    /// The depth ratio now over that when fresh: 1 for a fresh crater, falling as it degrades.
    double freshness = 1.0;
    bool complex = false;  ///< Of the complex form: a flat floor and a central peak.

    /// Memberwise equality.
    bool operator==(const CraterAttributes& other) const {
        return floorDepth == other.floorDepth && rimHeight == other.rimHeight &&
               depthRatio == other.depthRatio && freshness == other.freshness &&
               complex == other.complex;
    }
    bool operator!=(const CraterAttributes& other) const {
        return !(*this == other);
    }
};

/// The attributes of a crater in `state` (which gives the ratios) shaped by `profile` (the form).
///
/// The floor is the profile's lowest point inside the rim and the rim crest its highest from 0.7
/// to 1.3 radii, both sampled every 0.01 radii.
CraterAttributes ComputeCraterAttributes(const Crater& crater, const CraterState& state,
                                         const CraterProfile& profile);

}  // namespace terraingen
