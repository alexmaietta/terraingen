#pragma once

#include "terraingen/features/craters/ages.h"
#include "terraingen/features/craters/population.h"

#include <memory>

namespace terraingen {

/// A crater's shape measures relative to its diameter.
struct CraterMorphometry {
    double depthRatio = 0.0;      ///< Rim-to-floor depth / diameter.
    double rimHeightRatio = 0.0;  ///< Rim height above the pre-impact surface / diameter.
};

/// A crater's degradation state: its morphometry when fresh and now. Profiles shape craters to the
/// current morphometry; some, like `MartinParkesDunstanProfile`, also use the fresh one to place
/// the rounded rim of a degraded crater.
struct CraterState {
    CraterMorphometry fresh;
    CraterMorphometry current;
};

/// Rim height / diameter of fresh lunar craters, 0.0368 (Fassett et al. 2022, Table 3; Stopar et
/// al. 2017 find about 0.04 with no size trend from 65 m to 10 km).
inline constexpr double kFreshRimHeightRatio = 0.036822095;

/// Fassett et al. (2022) initial rim-to-floor depth/diameter ratio of fresh lunar craters (their
/// Table 2): 0.10 up to 10 m, 0.11 below 40 m, 0.13 below 100 m, 0.15 below 200 m, 0.17 below 400 m
/// and 0.21 above. The values above 40 m follow Stopar et al. (2017); those below are extrapolated.
///
/// References:
/// - C. I. Fassett et al., "Topographic Diffusion Revisited: Small Crater Lifetime on the Moon and
///   Implications for Volatile Exploration", JGR Planets 127, e2022JE007510, 2022.
///   https://doi.org/10.1029/2022JE007510
/// - J. D. Stopar et al., Icarus 298, 2017 (fresh-crater d/D by size; also DSNE Table 3.4.1.2-1).
/// @throws std::domain_error if `diameter` is not positive and finite.
double FreshCraterDepthRatio(double diameter);

/// Maps each crater to its degradation state, from its diameter and age.
class DegradationModel {
  public:
    virtual ~DegradationModel() = default;

    /// @throws std::domain_error if the crater's diameter is not positive and finite, or the model
    /// is not defined for it.
    virtual CraterState State(const Crater& crater) const = 0;
};

/// Every crater stays fresh: depth from `FreshCraterDepthRatio` (or a fixed ratio) and rim height
/// `kFreshRimHeightRatio`.
class NoDegradation final : public DegradationModel {
  public:
    NoDegradation() = default;

    /// A fixed fresh depth/diameter ratio for every crater.
    /// @throws std::invalid_argument unless `depthRatio` is positive and finite.
    explicit NoDegradation(double depthRatio);

    CraterState State(const Crater& crater) const override;

  private:
    double depthRatio_ = 0.0;
};

/// Degrades craters linearly with freshness, scaling fresh depth and rim height by one erosion
/// factor e = c + (1 - c) * freshness, where c makes a crater at the end of its lifetime exactly
/// `cutoffDepthRatio` deep.
///
/// Scaling depth and rim together is the erosion factor of Martin, Parkes and Dunstan (2014,
/// PANGU). The default cutoff 0.04 is the visibility threshold of Fassett et al. (2022), after
/// Mahanti et al. (2018). Physically, degradation is fast when fresh and slows with age;
/// diffusion-based degradation will capture that.
class FreshnessDegradation final : public DegradationModel {
  public:
    /// @throws std::invalid_argument if `ages` is null or `cutoffDepthRatio` is not in (0, 0.1).
    explicit FreshnessDegradation(std::shared_ptr<const CraterAgeModel> ages,
                                  double cutoffDepthRatio = 0.04);

    CraterState State(const Crater& crater) const override;

  private:
    std::shared_ptr<const CraterAgeModel> ages_;
    double cutoffDepthRatio_;
};

}  // namespace terraingen
