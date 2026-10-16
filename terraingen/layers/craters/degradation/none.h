/// @file
/// @brief `NoDegradation`, a degradation model that keeps every crater fresh.
#pragma once

#include "terraingen/layers/craters/degradation/degradation_model.h"

#include <optional>

namespace terraingen {

/// @brief Every crater stays as `fresh` makes it, optionally with one depth/diameter ratio for all.
class NoDegradation final : public DegradationModel {
  public:
    /// @brief Makes the model.
    /// @param fresh Each crater's morphometry when fresh.
    /// @param depthRatio Depth over diameter for every crater, in place of the fresh one's.
    /// @throws std::invalid_argument for an empty `fresh`, or a `depthRatio` that is not positive
    /// and finite.
    explicit NoDegradation(FreshMorphometry fresh, std::optional<double> depthRatio = std::nullopt);

    /// @brief The crater's fresh state, as both fresh and current.
    /// @param crater The crater.
    /// @return The state.
    CraterState State(const Crater& crater) const override;

  private:
    FreshMorphometry fresh_;
    std::optional<double> depthRatio_;
};

}  // namespace terraingen
