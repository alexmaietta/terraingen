#pragma once

#include "terraingen/layers/craters/degradation/degradation_model.h"

#include <optional>

namespace terraingen {

/// Every crater stays as `fresh` makes it, optionally with one depth/diameter ratio for all.
class NoDegradation final : public DegradationModel {
  public:
    /// @throws std::invalid_argument for an empty `fresh`, or a `depthRatio` that is not positive
    /// and finite.
    explicit NoDegradation(FreshMorphometry fresh, std::optional<double> depthRatio = std::nullopt);

    CraterState State(const Crater& crater) const override;

  private:
    FreshMorphometry fresh_;
    std::optional<double> depthRatio_;
};

}  // namespace terraingen
