#pragma once

#include "terraingen/craters/ages/age_model.h"
#include "terraingen/craters/lunar/chronology.h"
#include "terraingen/distribution/size_frequency.h"

#include <memory>

namespace terraingen {

/// Ages of craters on a surface `ageGyr` old where small craters are in equilibrium.
///
/// In equilibrium, craters of a size are erased as fast as they form, so their count is the
/// production rate times their lifetime: tau(D) = n_eq(D) / n_prod(D), from the incremental
/// equilibrium density and present-day production rate (as in synthterrain, which uses 0.1 dex
/// bins rather than the derivative). The lifetime is capped at the surface age; craters whose
/// lifetime exceeds it are production limited and all retained.
///
/// Ages follow the formation rate of the Neukum et al. (2001) chronology over
/// [0, min(tau(D), T)], which is nearly uniform for short lifetimes and weighted towards the early
/// bombardment for production-limited craters, with no switch between the two regimes.
class SteadyStateAges final : public CraterAgeModel {
  public:
    /// @param productionRate A `ProductionRate` distribution, such as
    /// `ExtendedNeukumProductionFunction`.
    /// @param equilibrium A `Density` distribution, such as `TraskEquilibrium`.
    /// @throws std::invalid_argument for null or mismatched kinds.
    /// @throws std::domain_error if `ageGyr` is outside [0, `kMaxLunarAgeGyr`].
    SteadyStateAges(std::shared_ptr<const SizeFrequencyDistribution> productionRate,
                    std::shared_ptr<const SizeFrequencyDistribution> equilibrium, double ageGyr);

    double SurfaceAgeGyr() const {
        return ageGyr_;
    }

    /// tau(D) before capping at the surface age, in Gyr.
    /// @throws std::domain_error outside either distribution's valid range.
    double EquilibriumLifetimeGyr(double diameter) const;

  protected:
    double LifetimeAt(double diameter) const override;
    double SampleAgeAt(RandomStream& random, double diameter, double lifetimeGyr) const override;

  private:
    std::shared_ptr<const SizeFrequencyDistribution> productionRate_;
    std::shared_ptr<const SizeFrequencyDistribution> equilibrium_;
    double ageGyr_;
};

}  // namespace terraingen
