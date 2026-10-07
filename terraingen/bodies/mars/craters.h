#pragma once

#include "terraingen/layers/craters/degradation/state.h"
#include "terraingen/layers/craters/profiles/complex.h"

namespace terraingen {

/// Diameter at which fresh Martian craters become complex, 7 km, where Garvin et al. (2003) start
/// their complex fits (Pike 1980 finds the transition between 3 and 8 km).
inline constexpr double kMarsComplexCraterDiameter = 7000.0;

/// The morphometry of a fresh Martian crater `diameter` metres across, D in km for the fits:
/// - depth over diameter 0.23, the mean of new, dated decametre-scale impacts (Daubar et al. 2014),
///   or Garvin et al.'s (2003) simple-crater fit d = 0.21 D^0.81 where that is shallower (from
///   about 0.6 km), and their complex fit d = 0.36 D^0.49 from `kMarsComplexCraterDiameter`;
/// - rim height over diameter 0.03, that of the most pristine 10 m to 1.2 km craters at InSight's
///   site (Sweeney et al. 2018), or Garvin et al.'s simple fit h = 0.04 D^0.31 where that is lower
///   (from about 1.5 km), their complex fit h = 0.02 D^0.84, and h = 0.12 D^0.35 above 100 km.
/// A `FreshMorphometry` for the degradation models.
///
/// References:
/// - I. J. Daubar, C. Atwood-Stone, S. Byrne, A. S. McEwen and P. S. Russell, "The morphology of
///   small fresh craters on Mars and the Moon", JGR Planets 119:2620-2639, 2014.
///   https://doi.org/10.1002/2014JE004671
/// - J. Sweeney et al., "Degradation of 100-m-scale rocky ejecta craters at the InSight landing
///   site on Mars and implications for surface processes and erosion rates in the Hesperian and
///   Amazonian", JGR Planets 123:2732-2759, 2018. https://doi.org/10.1029/2018JE005618
/// - J. B. Garvin, S. E. H. Sakimoto and J. J. Frawley, "Craters on Mars: global geometric
///   properties from gridded MOLA topography", Sixth International Conference on Mars, abstract
///   3277, 2003, Table 1. https://www.lpi.usra.edu/meetings/sixthmars2003/pdf/3277.pdf
/// - R. J. Pike, "Formation of complex impact craters: evidence from Mars and other planets",
///   Icarus 43:1-19, 1980.
/// @throws std::domain_error if `diameter` is not positive and finite.
CraterMorphometry MarsFreshCraterMorphometry(double diameter);

/// The shape of fresh Martian complex craters for `ComplexCraterProfile`, from
/// `kMarsComplexCraterDiameter`: central peaks of height 0.04 D^0.51 and diameter 0.25 D^1.05
/// (Garvin et al. 2003, fitted 7 to 100 km, so held at 100 km beyond), D in km. No Martian fit of
/// the floor's diameter was found, so the lunar one of Pike (1977), 0.187 D^1.249, stands in.
ComplexCraterShape MarsComplexCraterShape();

}  // namespace terraingen
