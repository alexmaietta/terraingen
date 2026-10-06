#pragma once

#include "terraingen/distribution/power_law.h"
#include "terraingen/distribution/tabulated.h"

#include <memory>

namespace terraingen {

/// Which of the minimum, nominal and maximum curves of an engineering model to use.
enum class RockModelCurve { Minimum, Nominal, Maximum };

/// Blocks per m^2 at least D metres across between craters on smooth mare, rough mare and upland
/// terrains: Figure 7 of NASA SP-8023, the lunar surface design model, which its authors drew as a
/// compromise between the Surveyor block counts of Shoemaker and Morris and of Choate.
///
/// The curves are published only as a figure. These tables were digitized from a 300 dpi scan of
/// it, reading each curve's crossings and fitting the straight segments; they agree with the
/// printed curves to about 0.03 in log10 N. They span 0.03 mm to about 6 to 9 m (to 10^-6 blocks
/// per m^2); the maximum curve steepens from about -1.8 to -3.1 at 0.24 m and the nominal from
/// about -2.1 to -3.2 near 0.85 m, while the minimum keeps a slope of about -2.55.
///
/// References:
/// - NASA, "Lunar Surface Models", Space Vehicle Design Criteria (Environment), NASA SP-8023, 1969,
///   section 3.4.2 and Figure 7. https://ntrs.nasa.gov/citations/19700009596
/// - E. M. Shoemaker and E. C. Morris, "Physical characteristics of the lunar regolith determined
///   from Surveyor television observations", Radio Science 5(2):129-155, 1970.
///   https://doi.org/10.1029/RS005i002p00129
std::shared_ptr<TabulatedSfd> Sp8023IntercraterBlocks(RockModelCurve curve);

/// Blocks per m^2 at least D metres across in the annulus from the rim to two crater radii of
/// blocky craters on smooth mare, rough mare and upland terrains (after H. J. Moore): Figure 9 of
/// NASA SP-8023, the model for the ejecta of craters that reached bedrock.
///
/// Digitized as `Sp8023IntercraterBlocks`, to about 0.03 in log10 N. The tables span from where
/// each curve leaves the top of the figure (10^-2 blocks per m^2, at 0.85 m for the nominal and
/// minimum curves and 1.4 m for the maximum) to 10^-6 blocks per m^2 (7 to 24 m). None is a power
/// law: the maximum curve steepens from a slope of about -2.1 to -4.5.
///
/// Reference: NASA SP-8023, 1969, section 3.4.3 and Figure 9.
/// https://ntrs.nasa.gov/citations/19700009596
std::shared_ptr<TabulatedSfd> Sp8023CraterBlocks(RockModelCurve curve);

/// The rock distributions synthterrain offers, N per m^2 with D in metres, for comparison with it.
/// Its sources carry no citation for them.
///
/// Reference: synthterrain (NASA Ames, R. Beyer et al.), `synthterrain.rock.functions`.
/// https://github.com/NeoGeographyToolkit/synthterrain
enum class SynthterrainRocks {
    InterCrater,  ///< 0.0001 D^-1.75457: from synthetic_moon, "low blockiness".
    Haworth,      ///< 0.002 D^-2.6607: from synthetic_moon, "high blockiness".
    /// 0.0003 D^-2.482: the VIPER Environmental Specification, VIPER-MSE-SPEC-001 (2021).
    ViperSpecification,
};

/// The synthterrain rock distribution `rocks`, valid at every diameter.
std::shared_ptr<PowerLawSfd> SynthterrainRockDistribution(SynthterrainRocks rocks);

}  // namespace terraingen
