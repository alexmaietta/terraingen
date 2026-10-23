/// @file
/// @brief The crater density of a surface of a given age: a production function times a chronology.
#pragma once

#include "terraingen/distribution/size_frequency.h"

#include <functional>
#include <memory>

namespace terraingen {

/// N(>=1 km) per m^2 on a surface `ageGyr` old, such as `NeukumLunarChronology`.
using Chronology = std::function<double(double ageGyr)>;

/// The cumulative crater density on a surface of a given age: a production function scaled
/// so that N(>=1 km) follows a chronology, N(D, T) = Phi(D) * N1(T) / Phi(1 km).
///
/// This is the population with no erosion; small craters exceed equilibrium on old surfaces.
class ProductionSurface final : public SizeFrequencyDistribution {
  public:
    /// The surface `ageGyr` Gyr old.
    /// @param production A `ProductionRate` distribution defined at 1 km. The surface takes its
    /// valid range and extrapolation setting.
    /// @param chronology N(>=1 km) per m^2 for the age, which may reject ages out of its range.
    /// @throws std::invalid_argument if `production` is null or not a production rate, or
    /// `chronology` is empty.
    /// @throws std::domain_error if the age is out of range or `production` is not defined at 1 km.
    ProductionSurface(std::shared_ptr<const SizeFrequencyDistribution> production, double ageGyr,
                      const Chronology& chronology);

    /// The `ProductionRate` distribution the surface scales.
    const SizeFrequencyDistribution& Production() const {
        return *production_;
    }

    /// The surface age in Gyr.
    double AgeGyr() const {
        return ageGyr_;
    }

  protected:
    double CumulativeAt(double diameter) const override;
    double IncrementalAt(double diameter) const override;
    double InvertCumulative(double target, double dMin, double dMax) const override;

  private:
    std::shared_ptr<const SizeFrequencyDistribution> production_;
    double ageGyr_;
    double scale_;
};

namespace detail {

/// A chronology N1(T) = `burst` (e^(`decay` T) - 1) + `linear` T per km^2, the form of Neukum's
/// (lunar) and Ivanov's (Martian): craters at least 1 km across on a surface T Gyr old.
struct ExponentialChronology {
    double burst = 0.0;         ///< Coefficient of the exponential term, per km^2.
    double decay = 0.0;         ///< Rate of the exponential term, per Gyr.
    double linear = 0.0;        ///< Present-day flux, per km^2 per Gyr.
    double maxAgeGyr = 0.0;     ///< Oldest age accepted, in Gyr.
    const char* ageError = "";  ///< Message for an age outside [0, `maxAgeGyr`].

    /// N1 per m^2 at `ageGyr` Gyr.
    /// @throws std::domain_error with `ageError` unless 0 <= `ageGyr` <= `maxAgeGyr`.
    double Count(double ageGyr) const;

    /// dN1/dT per m^2 per Gyr at `ageGyr` Gyr before present.
    /// @throws std::domain_error as `Count`.
    double Rate(double ageGyr) const;
};

}  // namespace detail

}  // namespace terraingen
