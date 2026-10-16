/// @file
/// @brief `DegradationModel`, which maps craters to their degradation state, and `NoDegradation`.
#pragma once

#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/craters/degradation/state.h"

#include <optional>

namespace terraingen {

/// @brief Maps each crater to its degradation state, from its diameter and age.
class DegradationModel {
  public:
    virtual ~DegradationModel() = default;

    /// @brief The degradation state of a crater.
    /// @param crater The crater; its diameter and age are read.
    /// @return Its fresh and current morphometry.
    /// @throws std::domain_error if the crater's diameter is not positive and finite, or the model
    /// is not defined for it.
    virtual CraterState State(const Crater& crater) const = 0;
};

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
