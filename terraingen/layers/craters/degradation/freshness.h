#pragma once

#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/layers/craters/degradation/degradation_model.h"

#include <memory>

namespace terraingen {

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
