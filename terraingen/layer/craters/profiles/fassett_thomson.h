#pragma once

#include "terraingen/layer/craters/profiles/profile.h"

namespace terraingen {

/// How a `FassettThomsonProfile` joins its interior and exterior fits at the rim.
enum class FassettThomsonRim {
    /// The published fits, which meet within 1e-5 D at 0.98 radii.
    Original,
    /// The interior fit extended to 1.0 radii so the rim lies at the stated diameter, as
    /// synthterrain's default `FT_Crater`; leaves a 0.0076 D step at the rim.
    AtOneRadius,
    /// Fassett et al. (2022) Table 3, synthterrain's `FTmod_Crater`: a flat rim of 0.0368 D from
    /// 0.98 to 1.02 radii; leaves a 0.0042 D step at 1.02 radii.
    Plateau,
};

/// Fresh simple-crater profile of Fassett and Thomson (2014, eq. 4), with the coefficients and
/// variants used by synthterrain. With x = distance / radius and heights in units of D:
///
///     -0.181                                                    x <= 0.2  (Original, AtOneRadius)
///     -0.228809953 + 0.227533882x + 0.083116795x^2 - 0.039499407x^3   interior
///      0.188253307 - 0.187050452x + 0.01844746x^2 + 0.01505647x^3     exterior, to x = 1.5
///
/// Every variant flattens the interior at a floor `state.current.depthRatio` below the fresh rim
/// height 0.0368 D, as Fassett et al. (2022) do for size-dependent fresh depths; a depth beyond the
/// fit's floor leaves the fit unchanged. The rim height is fixed by the fit, so degraded rim
/// heights are not represented.
///
/// References:
/// - C. I. Fassett and B. J. Thomson, "Crater degradation on the lunar maria: Topographic diffusion
///   and the rate of erosion on the Moon", JGR Planets 119:2255-2271, 2014.
///   https://doi.org/10.1002/2014JE004698
/// - C. I. Fassett et al., JGR Planets 127, e2022JE007510, 2022 (Table 3).
/// - synthterrain (NASA Ames), `synthterrain.crater.profile`: `FT_Crater` and `FTmod_Crater`.
///   https://github.com/NeoGeographyToolkit/synthterrain
class FassettThomsonProfile final : public CraterProfile {
  public:
    explicit FassettThomsonProfile(FassettThomsonRim rim = FassettThomsonRim::Original)
        : rim_(rim) {}

    double Height(double distance, double diameter, const CraterState& state) const override;

    double ReachRadii() const override {
        return 1.5;
    }

  private:
    FassettThomsonRim rim_;
};

}  // namespace terraingen
