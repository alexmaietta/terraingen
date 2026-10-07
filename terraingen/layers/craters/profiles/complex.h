#pragma once

#include "terraingen/layers/craters/profiles/profile.h"

#include <memory>

namespace terraingen {

/// y = `coefficient` * D^`exponent` with D and y in kilometres, the form morphometric relations are
/// published in (or km^3 for volumes).
struct KilometrePowerLaw {
    double coefficient = 0.0;
    double exponent = 0.0;

    /// y for a diameter of `diameter` metres, in metres (or m^3 with `volume`).
    double At(double diameter, bool volume = false) const;

    bool operator==(const KilometrePowerLaw&) const = default;
};

/// The shape of complex craters on a body, from the diameter they start at: their flat floor and
/// central peak. Their depth and rim height come from the crater's state (`FreshMorphometry`).
struct ComplexCraterShape {
    double transitionDiameter = 0.0;  ///< Smallest complex crater (m).
    KilometrePowerLaw floorDiameter;  ///< Diameter of the flat floor.
    KilometrePowerLaw peakHeight;     ///< Height of the central peak above the floor.
    KilometrePowerLaw peakVolume;     ///< Volume of the central peak (km^3).
    /// Largest crater the peak relations were fitted to (m); larger craters keep the peak height
    /// of this size and a base radius in proportion to the diameter, as peaks stop growing (Pike
    /// 1977) rather than follow the power laws out of range. 0 applies the laws to every size.
    double peakFitMaxDiameter = 0.0;
    double ejectaExponent = 3.0;  ///< The exterior falls off as (r / R)^-exponent.
    double reachRadii = 3.0;      ///< Radii from the centre where the exterior reaches zero.

    bool operator==(const ComplexCraterShape&) const = default;
};

/// @throws std::invalid_argument for a non-positive transition diameter, a power law with a
/// non-positive coefficient, a negative peak fit limit, a non-positive ejecta exponent, or a reach
/// of at most 1 radius.
void ValidateComplexCraterShape(const ComplexCraterShape& shape);

/// Simple craters up to a transition diameter and complex ones from it. With x = r / R, depth d and
/// rim height h from the crater's current state, a complex crater has:
/// - a flat floor at h - d out to the floor radius `floorDiameter(D)` / 2;
/// - a concave wall from the floor to the rim, h - d + d t^2 with t from 0 at the floor's edge to 1
///   at the rim, steepest just below the rim as terraced walls are;
/// - a central peak, a smooth dome h_p (1 - (r / r_b)^2)^2 whose height and volume follow
///   `peakHeight` and `peakVolume` (r_b^2 = 3 V / (pi h_p)), lowered with the crater as it wears
///   (by its depth now over its depth when fresh);
/// - an exterior falling from the rim as x^-`ejectaExponent`, the ejecta thickness law of McGetchin
///   et al. (1973), shifted to reach zero at `reachRadii`.
///
/// References:
/// - R. J. Pike, "Size-dependence in the shape of fresh impact craters on the Moon", in Impact and
///   Explosion Cratering, D. J. Roddy, R. O. Pepin and R. B. Merrill (eds), Pergamon, 489-510, 1977
///   (floor diameters and the transition).
/// - W. S. Hale and R. A. F. Grieve, "Volumetric analysis of complex lunar craters: implications
/// for
///   basin ring formation", Proc. Lunar Planet. Sci. Conf. 13, JGR 87:A65-A76, 1982 (central peak
///   heights and volumes).
/// - D. Stoffler et al., "Cratering history and lunar chronology", Reviews in Mineralogy and
///   Geochemistry 60:519-596, 2006, Table 5.1 (the relations above, collected).
///   https://doi.org/10.2138/rmg.2006.60.05
/// - T. R. McGetchin, M. Settle and J. W. Head, "Radial thickness variation in impact crater
///   ejecta: implications for lunar basin deposits", EPSL 20:226-236, 1973.
class ComplexCraterProfile final : public CraterProfile {
  public:
    /// @throws std::invalid_argument for a null `simple` or an invalid `shape`.
    ComplexCraterProfile(std::shared_ptr<const CraterProfile> simple, ComplexCraterShape shape);

    double Height(double distance, double diameter, const CraterState& state) const override;

    double ReachRadii() const override;

    bool IsComplex(double diameter) const override {
        return diameter >= shape_.transitionDiameter;
    }

    const ComplexCraterShape& Shape() const {
        return shape_;
    }

  private:
    std::shared_ptr<const CraterProfile> simple_;
    ComplexCraterShape shape_;
};

}  // namespace terraingen
