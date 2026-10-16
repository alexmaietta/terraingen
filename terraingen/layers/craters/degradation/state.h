/// @file
/// @brief A crater's shape measures and degradation state.
#pragma once

#include <functional>

namespace terraingen {

/// A crater's shape measures relative to its diameter.
struct CraterMorphometry {
    double depthRatio = 0.0;      ///< Rim-to-floor depth / diameter.
    double rimHeightRatio = 0.0;  ///< Rim height above the pre-impact surface / diameter.

    bool operator==(const CraterMorphometry&) const = default;
};

/// A crater's degradation state: its morphometry when fresh and now.
struct CraterState {
    CraterMorphometry fresh;    ///< When fresh; some profiles place a degraded rim from it.
    CraterMorphometry current;  ///< Now; profiles shape the crater to it.
    /// Diffusion age over diameter squared, kappa t / D^2, for profiles that diffuse the fresh
    /// shape (`DiffusedProfile`); 0 for none.
    double diffusion = 0.0;

    /// How fresh the crater looks: its depth now over its depth when fresh; 1 for a fresh crater
    /// (or one with no fresh depth), falling as it degrades.
    double Freshness() const {
        return fresh.depthRatio > 0.0 ? current.depthRatio / fresh.depthRatio : 1.0;
    }

    bool operator==(const CraterState&) const = default;
};

/// The morphometry of a fresh crater of a diameter (m) on some body, such as
/// `LunarFreshCraterMorphometry`; must be thread-safe.
using FreshMorphometry = std::function<CraterMorphometry(double diameter)>;

}  // namespace terraingen
