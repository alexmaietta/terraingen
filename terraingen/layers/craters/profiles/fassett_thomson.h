/// @file
/// @brief The fresh simple-crater profile of Fassett and Thomson (2014) and its rim variants.
#pragma once

#include "terraingen/layers/craters/profiles/profile.h"

namespace terraingen {

/// @brief How a `FassettThomsonProfile` joins its interior and exterior fits at the rim.
enum class FassettThomsonRim {
    Original,     ///< The published fits, which meet within 1e-5 D at 0.98 radii.
    AtOneRadius,  ///< Interior extended to 1.0 radii (synthterrain `FT_Crater`); 0.0076 D step.
    /// Fassett et al. (2022) Table 3 (synthterrain `FTmod_Crater`): flat at the fresh rim height
    /// from 0.98 to 1.02 radii.
    Plateau,
};

/// @brief Fresh simple-crater profile of Fassett and Thomson (2014, eq. 4): cubic interior and
/// exterior fits in x = r / R, reaching 1.5 radii.
///
/// The interior is flattened at a floor `state.current.depthRatio` below
/// `state.fresh.rimHeightRatio`; the rim height is fixed by the fit, so degraded rims are not
/// represented.
class FassettThomsonProfile final : public CraterProfile {
  public:
    /// @brief Makes the profile.
    /// @param rim How the fits join at the rim.
    explicit FassettThomsonProfile(FassettThomsonRim rim = FassettThomsonRim::Original)
        : rim_(rim) {}

    /// @brief The height of a crater at a distance from its centre.
    /// @param distance Distance from the centre, in metres.
    /// @param diameter Rim-to-rim diameter, in metres.
    /// @param state Degradation state; fresh rim height and current depth are read.
    /// @return Height relative to the pre-impact surface, in metres; 0 beyond 1.5 radii.
    double Height(double distance, double diameter, const CraterState& state) const override;

    /// @brief Distance beyond which the height is zero.
    /// @return 1.5 crater radii.
    double ReachRadii() const override {
        return 1.5;
    }

  private:
    FassettThomsonRim rim_;
};

}  // namespace terraingen
