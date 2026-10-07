#pragma once

#include "terraingen/layers/craters/degradation/diffusion.h"
#include "terraingen/layers/craters/degradation/state.h"
#include "terraingen/layers/craters/profiles/complex.h"

namespace terraingen {

/// Rim height / diameter of fresh lunar craters, 0.0368 (Fassett et al. 2022, Table 3; Stopar et
/// al. 2017 find about 0.04 with no size trend from 65 m to 10 km).
inline constexpr double kLunarFreshRimHeightRatio = 0.036822095;

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
double LunarFreshCraterDepthRatio(double diameter);

/// Diameter at which fresh lunar craters become complex, 15 km (Pike 1977); Kruger et al. (2018)
/// find that transitional and complex craters differ between the maria and the highlands.
inline constexpr double kLunarComplexCraterDiameter = 15000.0;

/// The morphometry of a fresh lunar crater `diameter` metres across. Simple craters, below
/// `kLunarComplexCraterDiameter`: `LunarFreshCraterDepthRatio` and `kLunarFreshRimHeightRatio`.
/// Complex craters: depth 1.044 D^0.301 (Pike 1974) and rim height 0.236 D^0.399 (Pike 1977), D and
/// both in km. A `FreshMorphometry` for the degradation models.
///
/// References:
/// - R. J. Pike, "Depth/diameter relations of fresh lunar craters: revision from spacecraft data",
///   Geophysical Research Letters 1:291-294, 1974. https://doi.org/10.1029/GL001i007p00291
/// - R. J. Pike, "Size-dependence in the shape of fresh impact craters on the Moon", in Impact and
///   Explosion Cratering, Pergamon, 489-510, 1977.
/// - T. Kruger, S. Hergarten and T. Kenkmann, "Deriving morphometric parameters and the
///   simple-to-complex transition diameter from a high-resolution, global database of fresh lunar
///   impact craters (D >= ~3 km)", JGR Planets 123:2667-2690, 2018.
///   https://doi.org/10.1029/2018JE005545
/// - D. Stoffler et al., "Cratering history and lunar chronology", Reviews in Mineralogy and
///   Geochemistry 60:519-596, 2006, Table 5.1.
/// @throws std::domain_error if `diameter` is not positive and finite.
CraterMorphometry LunarFreshCraterMorphometry(double diameter);

/// The shape of fresh lunar complex craters for `ComplexCraterProfile`, from
/// `kLunarComplexCraterDiameter`: floor diameter 0.187 D^1.249 (Pike 1977, fitted 20 to 125 km),
/// central peak height 0.589e-3 D^1.969 and volume 0.987e-7 D^5.078 km^3 (Hale and Grieve 1982,
/// fitted 17 to 51 km, so held at 51 km beyond), D in km, collected in Stoffler et al. (2006, Table
/// 5.1).
ComplexCraterShape LunarComplexCraterShape();

/// The lunar diffusivity of Fassett and Thomson (2014), 5.5 m^2/Myr, the average they find for 800
/// m to 5 km mare craters over the last 3 Gyr, for every size. With it a 1 km crater keeps about
/// 52% of its depth after 3 Gyr and a 300 m crater about 7%.
///
/// Reference: C. I. Fassett and B. J. Thomson, "Crater degradation on the lunar maria: Topographic
/// diffusion and the rate of erosion on the Moon", JGR Planets 119:2255-2271, 2014.
/// https://doi.org/10.1002/2014JE004698
PowerLawDiffusivity FassettThomson2014Diffusivity();

/// The size-dependent lunar diffusivity Fassett et al. (2022, Figure 2) find needed to hold the
/// Hartmann equilibrium of small craters, kappa = 3.6 D^0.87 m^2/Myr with D in km, fitted above 5 m
/// and held at its 5 m value below (where production is less certain). They find it 2 to 4 times
/// uncertain, and extrapolated to the kilometre scale it gives 2.8 to 3.2 m^2/Myr against
/// Fassett and Thomson's (2014) 5.5.
///
/// Reference: C. I. Fassett, R. A. Beyer, A. N. Deutsch, M. Hirabayashi, C. J. Leight,
/// P. Mahanti et al., "Topographic diffusion revisited: small crater lifetime on the Moon and
/// implications for volatile exploration", JGR Planets 127, e2022JE007510, 2022.
/// https://doi.org/10.1029/2022JE007510
PowerLawDiffusivity Fassett2022HartmannDiffusivity();

/// As `Fassett2022HartmannDiffusivity` for the Trask equilibrium: kappa = 3.1 D^1.05 m^2/Myr.
PowerLawDiffusivity Fassett2022TraskDiffusivity();

/// The diffusion age kappa t (m^2) Fassett and Thomson (2014, eq. 6) calibrate against crater age t
/// in Gyr for 800 m to 5 km mare craters: 435.83 t^5 - 3621.2 t^4 + 11204 t^3 - 16811 t^2 +
/// 17546 t, faster in the last 0.7 Gyr than before; clamped at 0. It does not depend on size, so
/// it suits kilometre-scale craters.
double FassettThomson2014DiffusionAge(double ageGyr);

}  // namespace terraingen
