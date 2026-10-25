// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `ComplexCraterProfile`: flat floors, concave walls and central peaks; `CraterTerraces`
/// steps the walls into slump terraces.
#pragma once

#include "terraingen/layers/craters/profiles/profile.h"

#include <memory>
#include <optional>

namespace terraingen {

/// y = `coefficient` * D^`exponent` with D and y in kilometers (km^3 for volumes), as
/// morphometric relations are published.
struct KilometerPowerLaw {
    double coefficient = 0.0;  ///< y of a 1 km crater, in km (or km^3).
    double exponent = 0.0;     ///< Power of the diameter.

    /// y in meters (m^3 with `volume`) for a crater of `diameter` meters: `KilometerLaw` in single
    /// precision.
    double At(double diameter, bool volume = false) const;

    /// Memberwise equality.
    bool operator==(const KilometerPowerLaw& other) const {
        return coefficient == other.coefficient && exponent == other.exponent;
    }
    bool operator!=(const KilometerPowerLaw& other) const {
        return !(*this == other);
    }
};

/// The flat floor and central peak of complex craters on a body, from the diameter they start
/// at; depth and rim height come from the crater's state.
struct ComplexCraterShape {
    double transitionDiameter = 0.0;  ///< Smallest complex crater (m).
    KilometerPowerLaw floorDiameter;  ///< Diameter of the flat floor.
    KilometerPowerLaw peakHeight;     ///< Height of the central peak above the floor.
    KilometerPowerLaw peakVolume;  ///< Volume of the central peak (km^3), with no `peakDiameter`.
    /// Diameter of the central peak's base, when known; otherwise the base follows from the peak's
    /// height and `peakVolume`.
    std::optional<KilometerPowerLaw> peakDiameter;
    /// Largest crater the peak relations were fitted to (m); larger craters keep this size's peak
    /// height and a base in proportion to the diameter; unset applies the laws to every size.
    std::optional<double> peakFitMaxDiameter{};
    double ejectaExponent = 3.0;  ///< The exterior falls off as (r / R)^-exponent.
    double reachRadii = 3.0;      ///< Radii from the center where the exterior reaches zero.

    /// Memberwise equality.
    bool operator==(const ComplexCraterShape& other) const {
        return transitionDiameter == other.transitionDiameter &&
               floorDiameter == other.floorDiameter && peakHeight == other.peakHeight &&
               peakVolume == other.peakVolume && peakDiameter == other.peakDiameter &&
               peakFitMaxDiameter == other.peakFitMaxDiameter &&
               ejectaExponent == other.ejectaExponent && reachRadii == other.reachRadii;
    }
    bool operator!=(const ComplexCraterShape& other) const {
        return !(*this == other);
    }
};

/// Checks a complex crater shape.
/// @throws std::invalid_argument for a non-positive transition diameter, a power law with a
/// non-positive coefficient, a negative peak fit limit, a non-positive ejecta exponent, or a reach
/// of at most 1 radius.
void ValidateComplexCraterShape(const ComplexCraterShape& shape);

/// Sets the complex form of `shape` on kernel profile `p`, as `ComplexProfileHeight` reads it.
void SetKernelComplexShape(CraterProfileKernel& p, const ComplexCraterShape& shape);

/// Simple craters below a transition diameter, complex ones from it: a flat floor, a concave
/// wall, a central peak lowered as the crater wears, and a power-law exterior.
class ComplexCraterProfile final : public CraterProfile {
  public:
    /// @param simple The profile of simple craters.
    /// @throws std::invalid_argument for a null `simple` or an invalid `shape`.
    ComplexCraterProfile(std::shared_ptr<const CraterProfile> simple, ComplexCraterShape shape);

    /// The simple profile below the transition, else `ComplexProfileHeight` in single precision.
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
    CraterProfileKernel kernel_;
};

}  // namespace terraingen
