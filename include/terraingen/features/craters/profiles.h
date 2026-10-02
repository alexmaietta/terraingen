#pragma once

#include "terraingen/features/craters/degradation.h"

namespace terraingen {

/// The topography of a crater: height relative to the pre-impact surface as a function of distance
/// from the centre, for a given degradation state.
class CraterProfile {
  public:
    virtual ~CraterProfile() = default;

    /// Height in metres, relative to the surface the crater formed on, at `distance` metres from
    /// the centre of a crater `diameter` metres across (rim to rim) in degradation state `state`.
    virtual double Height(double distance, double diameter, const CraterState& state) const = 0;

    /// Distance from the centre, in crater radii, beyond which `Height` is zero.
    virtual double ReachRadii() const = 0;
};

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

/// Simple-crater profile of Martin, Parkes and Dunstan (2014), developed for the PANGU planetary
/// surface simulator at the University of Dundee: four connected polynomials in
/// x = 2 r / D - 1 (-1 at the centre, 0 at the rim, 1 at two radii).
///
/// With K = H_r0 - H_r + H, from the fresh rim height H_r0 and current depth H and rim height H_r:
///
///     h1 = K x^2 + 2K x + H_r0                    -1 <= x <= alpha   bowl (eq. 4)
///     h2 = K (alpha + 1) / alpha x^2 + H_r        alpha <= x <= 0    inner rim (eq. 5)
///     h3 = a x^3 + b x^2 + H_r                    0 <= x <= beta     outer rim
///     h4 = 0.14 (D/2)^0.74 (x + 1)^-3 + F_c(x)    beta <= x <= 1     ejecta (eq. 9, McGetchin
///     1974)
///
/// with alpha = (H_r - H_r0) / K (eq. 6). A fresh crater (H_r = H_r0) has alpha = beta = 0 and a
/// sharp rim; degradation moves alpha inwards and beta outwards, rounding the rim as in Ross's
/// model. The terrain and plane heights T_r and P_r of the paper are zero here, because
/// `ApplyCraters` places the profile on a plane fitted to the crater.
///
/// Departures from the published equations, which give discontinuous profiles:
/// - beta = 3 (H_r0 - H_r) / (2K), with the sign of eq. 8 reversed so it is positive (as in
///   synthterrain);
/// - h3 is synthterrain's cubic, flat at the rim and meeting h4 at beta, in place of eq. 7;
/// - F_c is the linear function that meets the paper's stated constraints, h4(0) = H_r and
///   h4(1) = 0 (the ejecta joins the rim and ends at the surrounding surface), in place of eq. 10;
/// - fresh and current depth and rim height come from the degradation state, rather than from
///   Melosh's H_0 = 0.196 D^1.01 and H_r0 = 0.036 D^1.01 times an erosion factor
///   (`FreshnessDegradation` provides that erosion factor).
///
/// The fractal roughness overlays and irregular rims of the paper are separate perturbations, not
/// part of this radial profile.
///
/// References:
/// - S. Martin, S. Parkes and M. Dunstan, "Modeling cratered surfaces with real and synthetic
///   terrain for testing planetary landers", IEEE Transactions on Aerospace and Electronic Systems
///   50(4):2916-2928, 2014. https://doi.org/10.1109/TAES.2014.120282
/// - T. R. McGetchin, M. Settle and J. W. Head, "Radial thickness variation in impact crater
/// ejecta:
///   Implications for lunar basin deposits", Earth and Planetary Science Letters 23, 1974.
/// - synthterrain (NASA Ames), `synthterrain.crater.profile.MPD_Crater`.
class MartinParkesDunstanProfile final : public CraterProfile {
  public:
    /// @throws std::invalid_argument if the state's depth is not positive or its current rim height
    /// is negative or exceeds the fresh rim height.
    double Height(double distance, double diameter, const CraterState& state) const override;

    double ReachRadii() const override {
        return 2.0;
    }
};

}  // namespace terraingen
