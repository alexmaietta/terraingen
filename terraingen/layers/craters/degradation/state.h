#pragma once

#include <functional>

namespace terraingen {

/// A crater's shape measures relative to its diameter.
struct CraterMorphometry {
    double depthRatio = 0.0;      ///< Rim-to-floor depth / diameter.
    double rimHeightRatio = 0.0;  ///< Rim height above the pre-impact surface / diameter.

    bool operator==(const CraterMorphometry&) const = default;
};

/// A crater's degradation state: its morphometry when fresh and now. Profiles shape craters to the
/// current morphometry; some, like `MartinParkesDunstanProfile`, also use the fresh one to place
/// the rounded rim of a degraded crater.
struct CraterState {
    CraterMorphometry fresh;
    CraterMorphometry current;

    bool operator==(const CraterState&) const = default;
};

/// The morphometry of a fresh crater of a given diameter (m) on some body, such as
/// `LunarFreshCraterMorphometry`; must be safe to call from several threads at once.
using FreshMorphometry = std::function<CraterMorphometry(double diameter)>;

}  // namespace terraingen
