/// @file
/// @brief `FreshnessDegradation`, craters degraded linearly with the share of their lifetime left.
#pragma once

#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/layers/craters/degradation/degradation_model.h"

#include <memory>

namespace terraingen {

/// Scales fresh depth and rim height by e = c + (1 - c) * `RemainingLifetime`, with c
/// making a crater at the end of its lifetime exactly `cutoffDepthRatio` deep; a crater already
/// shallower than that when fresh, such as a large basin, keeps its fresh shape.
class FreshnessDegradation final : public DegradationModel {
  public:
    /// @param cutoffDepthRatio Depth over diameter at the end of a crater's lifetime.
    /// @throws std::invalid_argument if `ages` is null, `fresh` empty, or `cutoffDepthRatio` not
    /// in (0, 0.1).
    FreshnessDegradation(std::shared_ptr<const CraterAgeModel> ages, FreshMorphometry fresh,
                         double cutoffDepthRatio = 0.04);

    /// The crater's fresh morphometry, and that scaled by the erosion factor at its age.
    /// @throws std::domain_error as `CraterAgeModel::LifetimeGyr`.
    CraterState State(const Crater& crater) const override;

  private:
    std::shared_ptr<const CraterAgeModel> ages_;
    FreshMorphometry fresh_;
    double cutoffDepthRatio_;
};

}  // namespace terraingen
