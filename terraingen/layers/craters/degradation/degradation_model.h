/// @file
/// @brief `DegradationModel`, which maps craters to their degradation state, and `NoDegradation`.
#pragma once

#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/craters/degradation/state.h"

#include <optional>

namespace terraingen {

/// Maps each crater to its degradation state, from its diameter and age.
class DegradationModel {
  public:
    virtual ~DegradationModel() = default;

    /// A crater's fresh and current morphometry, from its diameter and age.
    /// @throws std::domain_error if the crater's diameter is not positive and finite, or the model
    /// is not defined for it.
    virtual CraterState State(const Crater& crater) const = 0;
};

/// Every crater stays as `fresh` makes it, optionally with one depth/diameter ratio for all.
class NoDegradation final : public DegradationModel {
  public:
    /// @param depthRatio Depth over diameter for every crater, in place of the fresh one's.
    /// @throws std::invalid_argument for an empty `fresh`, or a `depthRatio` that is not positive
    /// and finite.
    explicit NoDegradation(FreshMorphometry fresh, std::optional<double> depthRatio = std::nullopt);

    /// The crater's fresh state, as both fresh and current.
    CraterState State(const Crater& crater) const override;

  private:
    FreshMorphometry fresh_;
    std::optional<double> depthRatio_;
};

}  // namespace terraingen
