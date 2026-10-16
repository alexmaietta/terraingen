/// @file
/// @brief Crater ages on lunar surfaces where small craters are in equilibrium.
#pragma once

#include "terraingen/bodies/moon/chronology.h"
#include "terraingen/distribution/size_frequency.h"
#include "terraingen/layers/craters/ages/age_model.h"

#include <memory>

namespace terraingen {

/// @brief Ages of craters on a surface `ageGyr` old where small craters are in equilibrium.
///
/// A crater's lifetime is tau(D) = n_eq(D) / n_prod(D), capped at the surface age; ages follow the
/// Neukum et al. (2001) formation rate over [0, lifetime].
class SteadyStateAges final : public CraterAgeModel {
  public:
    /// @brief The ages from a production rate and an equilibrium density.
    /// @param productionRate A `ProductionRate` distribution, such as
    /// `ExtendedNeukumProductionFunction`.
    /// @param equilibrium A `Density` distribution, such as `TraskEquilibrium`.
    /// @param ageGyr Surface age in Gyr.
    /// @throws std::invalid_argument for null or mismatched kinds.
    /// @throws std::domain_error if `ageGyr` is outside [0, `kMaxLunarAgeGyr`].
    SteadyStateAges(std::shared_ptr<const SizeFrequencyDistribution> productionRate,
                    std::shared_ptr<const SizeFrequencyDistribution> equilibrium, double ageGyr);

    /// @brief The surface age.
    /// @return Age in Gyr.
    double SurfaceAgeGyr() const {
        return ageGyr_;
    }

    /// @brief tau(D) before capping at the surface age.
    /// @param diameter Crater diameter in metres.
    /// @return The equilibrium lifetime in Gyr.
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
