// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/degradation/degradation_model.h"

#include "terraingen/util/checks.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

CraterState DegradationModel::WornState(const Crater& crater, double rate) const {
    Crater older = crater;
    older.ageGyr *= rate;
    return State(older);
}

std::shared_ptr<const CraterProfile> MatchedProfile(std::shared_ptr<const CraterProfile> profile,
                                                    const DegradationModel& degradation) {
    std::shared_ptr<const CraterProfile> required = degradation.RequiredProfile();
    if (!profile && !required) {
        throw std::invalid_argument("Craters need a profile");
    }
    if (profile && required && profile != required) {
        throw std::invalid_argument(
            "The degradation shapes craters with its own profile; pass that profile or none");
    }
    return profile ? profile : required;
}
NoDegradation::NoDegradation(FreshMorphometry fresh, std::optional<double> depthRatio)
    : fresh_(std::move(fresh)), depthRatio_(depthRatio) {
    if (!fresh_) {
        throw std::invalid_argument("No degradation needs the fresh crater morphometry");
    }
    if (depthRatio_) {
        RequirePositive(*depthRatio_, "Depth/diameter ratio");
    }
}

CraterState NoDegradation::State(const Crater& crater) const {
    CraterMorphometry fresh = fresh_(crater.diameter);
    if (depthRatio_) {
        fresh.depthRatio = *depthRatio_;
    }
    return {fresh, fresh};
}

}  // namespace terraingen
