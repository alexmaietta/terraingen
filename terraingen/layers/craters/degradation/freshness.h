/// @file
/// @brief `FreshnessDegradation`, craters degraded linearly with the share of their lifetime left.
#pragma once

#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/layers/craters/degradation/degradation_model.h"

#include <memory>

namespace terraingen {

/// @brief Scales fresh depth and rim height by e = c + (1 - c) * `RemainingLifetime`, with c
/// making a crater at the end of its lifetime exactly `cutoffDepthRatio` deep.
class FreshnessDegradation final : public DegradationModel {
  public:
    /// @brief Builds the model.
    /// @param ages The age model giving each crater's lifetime.
    /// @param fresh Each crater's morphometry when fresh.
    /// @param cutoffDepthRatio Depth over diameter at the end of a crater's lifetime.
    /// @throws std::invalid_argument if `ages` is null, `fresh` empty, or `cutoffDepthRatio` not
    /// in (0, 0.1).
    FreshnessDegradation(std::shared_ptr<const CraterAgeModel> ages, FreshMorphometry fresh,
                         double cutoffDepthRatio = 0.04);

    /// @brief The crater's state at its age.
    /// @param crater The crater.
    /// @return Its fresh morphometry, and that scaled by the erosion factor.
    /// @throws std::domain_error as `CraterAgeModel::LifetimeGyr`.
    CraterState State(const Crater& crater) const override;

  private:
    std::shared_ptr<const CraterAgeModel> ages_;
    FreshMorphometry fresh_;
    double cutoffDepthRatio_;
};

}  // namespace terraingen
