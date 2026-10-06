#pragma once

#include "terraingen/features/craters/profiles/profile.h"

namespace terraingen {

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
