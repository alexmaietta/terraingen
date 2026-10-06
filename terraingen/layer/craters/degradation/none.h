#pragma once

#include "terraingen/layer/craters/degradation/degradation_model.h"

namespace terraingen {

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

}  // namespace terraingen
