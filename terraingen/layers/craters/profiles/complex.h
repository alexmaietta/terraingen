/// @file
/// @brief `ComplexCraterProfile`: flat floors, terraced walls and central peaks.
#pragma once

#include "terraingen/layers/craters/profiles/profile.h"

#include <memory>
#include <optional>

namespace terraingen {

/// y = `coefficient` * D^`exponent` with D and y in kilometres (km^3 for volumes), as
/// morphometric relations are published.
struct KilometrePowerLaw {
    double coefficient = 0.0;  ///< y of a 1 km crater, in km (or km^3).
    double exponent = 0.0;     ///< Power of the diameter.

    /// y in metres (m^3 with `volume`) for a crater of `diameter` metres.
    double At(double diameter, bool volume = false) const;

    bool operator==(const KilometrePowerLaw&) const = default;
};

/// The flat floor and central peak of complex craters on a body, from the diameter they start
/// at; depth and rim height come from the crater's state.
struct ComplexCraterShape {
    double transitionDiameter = 0.0;  ///< Smallest complex crater (m).
    KilometrePowerLaw floorDiameter;  ///< Diameter of the flat floor.
    KilometrePowerLaw peakHeight;     ///< Height of the central peak above the floor.
    KilometrePowerLaw peakVolume;  ///< Volume of the central peak (km^3), with no `peakDiameter`.
    /// Diameter of the central peak's base, when known; otherwise the base follows from the peak's
    /// height and `peakVolume`.
    std::optional<KilometrePowerLaw> peakDiameter;
    /// Largest crater the peak relations were fitted to (m); larger craters keep this size's peak
    /// height and a base in proportion to the diameter. 0 applies the laws to every size.
    double peakFitMaxDiameter = 0.0;
    double ejectaExponent = 3.0;  ///< The exterior falls off as (r / R)^-exponent.
    double reachRadii = 3.0;      ///< Radii from the centre where the exterior reaches zero.

    bool operator==(const ComplexCraterShape&) const = default;
};

/// Checks a complex crater shape.
/// @throws std::invalid_argument for a non-positive transition diameter, a power law with a
/// non-positive coefficient, a negative peak fit limit, a non-positive ejecta exponent, or a reach
/// of at most 1 radius.
void ValidateComplexCraterShape(const ComplexCraterShape& shape);

/// Simple craters below a transition diameter, complex ones from it: a flat floor, a concave
/// wall, a central peak lowered as the crater wears, and a power-law exterior.
class ComplexCraterProfile final : public CraterProfile {
  public:
    /// @param simple The profile of simple craters.
    /// @throws std::invalid_argument for a null `simple` or an invalid `shape`.
    ComplexCraterProfile(std::shared_ptr<const CraterProfile> simple, ComplexCraterShape shape);

    double Height(double distance, double diameter, const CraterState& state) const override;

    /// The larger of the simple profile's reach and `reachRadii`.
    double ReachRadii() const override;

    /// True from `transitionDiameter`.
    bool IsComplex(double diameter) const override {
        return diameter >= shape_.transitionDiameter;
    }

    /// The shape the profile was made with.
    const ComplexCraterShape& Shape() const {
        return shape_;
    }

    /// The profile of simple craters.
    const CraterProfile& Simple() const {
        return *simple_;
    }

  private:
    std::shared_ptr<const CraterProfile> simple_;
    ComplexCraterShape shape_;
};

}  // namespace terraingen
