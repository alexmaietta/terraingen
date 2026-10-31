// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/degradation/degradation_model.h"

#include "terraingen/layers/craters/profiles/martin_parkes_dunstan.h"
#include "util/checks.h"

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
    if (profile && required && profile != required) {
        throw std::invalid_argument(
            "The degradation shapes craters with its own profile; pass that profile or none");
    }
    if (profile) {
        return profile;
    }
    return required ? required : std::make_shared<MartinParkesDunstanProfile>();
}
NoDegradation::NoDegradation(FreshMorphometry fresh, std::optional<double> depthRatio)
    : m_fresh(std::move(fresh)), m_depthRatio(depthRatio) {
    if (!m_fresh) {
        throw std::invalid_argument("No degradation needs the fresh crater morphometry");
    }
    if (m_depthRatio) {
        RequirePositive(*m_depthRatio, "Depth/diameter ratio");
    }
}

CraterState NoDegradation::State(const Crater& crater) const {
    CraterMorphometry fresh = m_fresh(crater.diameter);
    if (m_depthRatio) {
        fresh.depthRatio = *m_depthRatio;
    }
    return {fresh, fresh};
}

}  // namespace terraingen
