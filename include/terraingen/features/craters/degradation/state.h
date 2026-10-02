#pragma once

namespace terraingen {

/// A crater's shape measures relative to its diameter.
struct CraterMorphometry {
    double depthRatio = 0.0;      ///< Rim-to-floor depth / diameter.
    double rimHeightRatio = 0.0;  ///< Rim height above the pre-impact surface / diameter.
};

/// A crater's degradation state: its morphometry when fresh and now. Profiles shape craters to the
/// current morphometry; some, like `MartinParkesDunstanProfile`, also use the fresh one to place
/// the rounded rim of a degraded crater.
struct CraterState {
    CraterMorphometry fresh;
    CraterMorphometry current;
};

/// Rim height / diameter of fresh lunar craters, 0.0368 (Fassett et al. 2022, Table 3; Stopar et
/// al. 2017 find about 0.04 with no size trend from 65 m to 10 km).
inline constexpr double kFreshRimHeightRatio = 0.036822095;

/// Fassett et al. (2022) initial rim-to-floor depth/diameter ratio of fresh lunar craters (their
/// Table 2): 0.10 up to 10 m, 0.11 below 40 m, 0.13 below 100 m, 0.15 below 200 m, 0.17 below 400 m
/// and 0.21 above. The values above 40 m follow Stopar et al. (2017); those below are extrapolated.
///
/// References:
/// - C. I. Fassett et al., "Topographic Diffusion Revisited: Small Crater Lifetime on the Moon and
///   Implications for Volatile Exploration", JGR Planets 127, e2022JE007510, 2022.
///   https://doi.org/10.1029/2022JE007510
/// - J. D. Stopar et al., Icarus 298, 2017 (fresh-crater d/D by size; also DSNE Table 3.4.1.2-1).
/// @throws std::domain_error if `diameter` is not positive and finite.
double FreshCraterDepthRatio(double diameter);

}  // namespace terraingen
