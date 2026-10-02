#pragma once

#include "terraingen/distributions/size_frequency.h"
#include "terraingen/features/craters/lunar/chronology.h"
#include "terraingen/random/random_stream.h"

#include <memory>

namespace terraingen {

/// Assigns ages to craters by diameter.
///
/// Each model has a visible lifetime per diameter, the span over which craters of that size are
/// found, and draws ages within it. Freshness, 1 - age / lifetime, expresses the same age as the
/// fraction of the lifetime remaining, for degradation models that work in freshness rather than
/// years.
class CraterAgeModel {
  public:
    virtual ~CraterAgeModel() = default;

    /// Visible lifetime of craters `diameter` metres across, in Gyr.
    /// @throws std::domain_error if `diameter` is not positive and finite, or the model is not
    /// defined there.
    double LifetimeGyr(double diameter) const;

    /// Age in Gyr, within [0, `LifetimeGyr(diameter)`], of a crater `diameter` metres across.
    /// @throws std::domain_error under the same conditions as `LifetimeGyr`.
    double SampleAgeGyr(RandomStream& random, double diameter) const;

    /// 1 - `ageGyr` / `LifetimeGyr(diameter)`, clamped to [0, 1]; 1 when the lifetime is zero.
    /// @throws std::domain_error under the same conditions as `LifetimeGyr`.
    double Freshness(double diameter, double ageGyr) const;

  protected:
    /// Lifetime for a valid diameter.
    virtual double LifetimeAt(double diameter) const = 0;

    /// Age for a valid diameter with lifetime `lifetimeGyr`.
    virtual double SampleAgeAt(RandomStream& random, double diameter, double lifetimeGyr) const = 0;
};

/// Every crater is fresh: age and lifetime zero.
class AllFresh final : public CraterAgeModel {
  protected:
    double LifetimeAt(double diameter) const override;
    double SampleAgeAt(RandomStream& random, double diameter, double lifetimeGyr) const override;
};

/// Ages uniform over one lifetime for every diameter, so freshness is uniform in [0, 1].
class UniformFreshness final : public CraterAgeModel {
  public:
    /// @throws std::invalid_argument unless `lifetimeGyr` is positive and finite.
    explicit UniformFreshness(double lifetimeGyr);

  protected:
    double LifetimeAt(double diameter) const override;
    double SampleAgeAt(RandomStream& random, double diameter, double lifetimeGyr) const override;

  private:
    double lifetimeGyr_;
};

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

/// A surface's crater density and age model, the inputs to crater generation.
struct SurfaceModel {
    std::shared_ptr<const SizeFrequencyDistribution> density;
    std::shared_ptr<const CraterAgeModel> ages;
};

/// The DSNE construction of a lunar surface `ageGyr` old: density
/// min(`ExtendedNeukumProductionFunction` surface, `TraskEquilibrium`), valid from 2.5 m to 300 km,
/// with `SteadyStateAges` from the same two distributions.
///
/// Reference: NASA SLS-SPEC-159 (DSNE) Revision I, Section 3.4.1.1.
/// @throws std::domain_error if `ageGyr` is outside [0, `kMaxLunarAgeGyr`].
SurfaceModel LunarSurface(double ageGyr);

}  // namespace terraingen
