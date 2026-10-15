/// @file
/// @brief `FreshnessDegradation`, craters degraded linearly with the share of their lifetime left.
#pragma once

#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/layers/craters/degradation/degradation_model.h"

#include <memory>

namespace terraingen {

/// Degrades craters linearly with the share of their lifetime left (`RemainingLifetime`), scaling
/// fresh depth and rim height by one erosion factor e = c + (1 - c) * remaining, where c makes a
/// crater at the end of its lifetime exactly `cutoffDepthRatio` deep. Real degradation is fast when
/// fresh and slows with age, as `DiffusionDegradation` models. See SCIENCE.md, "Crater
/// degradation".
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

    CraterState State(const Crater& crater) const override;

  private:
    std::shared_ptr<const CraterAgeModel> ages_;
    FreshMorphometry fresh_;
    double cutoffDepthRatio_;
};

}  // namespace terraingen
