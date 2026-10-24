// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `DegradationModel`, which maps craters to their degradation state, and `NoDegradation`.
#pragma once

#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/craters/degradation/state.h"

#include <memory>
#include <optional>

namespace terraingen {

class CraterAgeModel;
class CraterProfile;

/// Maps each crater to its degradation state, from its diameter and age.
class DegradationModel {
  public:
    virtual ~DegradationModel() = default;

    /// A crater's fresh and current morphometry, from its diameter and age.
    /// @throws std::domain_error if the crater's diameter is not positive and finite, or the model
    /// is not defined for it.
    virtual CraterState State(const Crater& crater) const = 0;

    /// The crater's state had it worn `rate` times as fast as on level ground, as on a steep
    /// regional slope. By default it is `State` of the crater `rate` times older, which suits
    /// models whose wear follows age alone.
    /// @param rate At least 1.
    /// @throws std::domain_error as `State`.
    virtual CraterState WornState(const Crater& crater, double rate) const;

    /// The profile craters in this model's states must be shaped with; null when any will do.
    virtual std::shared_ptr<const CraterProfile> RequiredProfile() const {
        return nullptr;
    }

    /// The age model whose lifetimes this model's states follow, which a crater layer given no
    /// ages draws from; null for none.
    virtual std::shared_ptr<const CraterAgeModel> AgeModel() const {
        return nullptr;
    }
};

/// The profile to shape craters under `degradation` with: `profile`, or when it is null the one
/// `degradation` requires, else Martin, Parkes and Dunstan's (`MartinParkesDunstanProfile`).
/// @throws std::invalid_argument if `profile` is set and is not the one `degradation` requires.
std::shared_ptr<const CraterProfile> MatchedProfile(std::shared_ptr<const CraterProfile> profile,
                                                    const DegradationModel& degradation);

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
