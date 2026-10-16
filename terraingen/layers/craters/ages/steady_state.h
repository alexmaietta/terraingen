/// @file
/// @brief Crater ages on surfaces where small craters are in equilibrium.
#pragma once

#include "terraingen/distribution/production_surface.h"
#include "terraingen/distribution/size_frequency.h"
#include "terraingen/layers/craters/ages/age_model.h"

#include <memory>

namespace terraingen {

/// Ages of craters on a surface `ageGyr` old where small craters are in equilibrium.
///
/// A crater's lifetime is tau(D) = n_eq(D) / n_prod(D), capped at the surface age; ages follow the
/// formation rate of `chronology` over [0, lifetime].
class SteadyStateAges final : public CraterAgeModel {
  public:
    /// @param productionRate A `ProductionRate` distribution, such as
    /// `ExtendedNeukumProductionFunction`.
    /// @param equilibrium A `Density` distribution, such as `TraskEquilibrium`.
    /// @param ageGyr Surface age in Gyr.
    /// @param chronology N(>=1 km) per m^2 by age, such as `NeukumLunarChronology`; ages are drawn
    /// in proportion to its rate.
    /// @throws std::invalid_argument for null or mismatched kinds, or an empty `chronology`.
    /// @throws std::domain_error if `chronology` rejects `ageGyr`.
    SteadyStateAges(std::shared_ptr<const SizeFrequencyDistribution> productionRate,
                    std::shared_ptr<const SizeFrequencyDistribution> equilibrium, double ageGyr,
                    Chronology chronology);

    /// The surface age in Gyr.
    double SurfaceAgeGyr() const {
        return ageGyr_;
    }

    /// The equilibrium lifetime tau(D) in Gyr of craters of `diameter` metres, before capping at
    /// the surface age.
    /// @throws std::domain_error outside either distribution's valid range.
    double EquilibriumLifetimeGyr(double diameter) const;

  protected:
    double LifetimeAt(double diameter) const override;
    double SampleAgeAt(RandomStream& random, double diameter, double lifetimeGyr) const override;

  private:
    std::shared_ptr<const SizeFrequencyDistribution> productionRate_;
    std::shared_ptr<const SizeFrequencyDistribution> equilibrium_;
    double ageGyr_;
    Chronology chronology_;
};

}  // namespace terraingen
