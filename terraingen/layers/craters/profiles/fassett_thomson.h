/// @file
/// @brief The fresh simple-crater profile of Fassett and Thomson (2014) and its rim variants.
#pragma once

#include "terraingen/layers/craters/profiles/profile.h"

namespace terraingen {

/// How a `FassettThomsonProfile` joins its interior and exterior fits at the rim.
enum class FassettThomsonRim {
    Original,     ///< The published fits, which meet within 1e-5 D at 0.98 radii.
    AtOneRadius,  ///< Interior extended to 1.0 radii (synthterrain `FT_Crater`); 0.0076 D step.
    /// Fassett et al. (2022) Table 3 (synthterrain `FTmod_Crater`): flat at the fresh rim height
    /// from 0.98 to 1.02 radii.
    Plateau,
};

/// Fresh simple-crater profile of Fassett and Thomson (2014, eq. 4): cubic interior and
/// exterior fits in x = r / R, reaching 1.5 radii.
///
/// The interior is flattened at a floor `state.current.depthRatio` below
/// `state.fresh.rimHeightRatio`; the rim height is fixed by the fit, so degraded rims are not
/// represented.
class FassettThomsonProfile final : public CraterProfile {
  public:
    /// @param rim How the interior and exterior fits are joined at the rim.
    explicit FassettThomsonProfile(FassettThomsonRim rim = FassettThomsonRim::Original)
        : rim_(rim) {}

    /// Reads the fresh rim height and current depth of `state`.
    double Height(double distance, double diameter, const CraterState& state) const override;

    double ReachRadii() const override {
        return 1.5;
    }

    /// How the fits are joined at the rim.
    FassettThomsonRim Rim() const {
        return rim_;
    }

  private:
    FassettThomsonRim rim_;
};

}  // namespace terraingen
