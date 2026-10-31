// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/degradation/freshness_degradation.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

FreshnessDegradation::FreshnessDegradation(std::shared_ptr<const CraterAgeModel> ages,
                                           FreshMorphometry fresh, double cutoffDepthRatio)
    : m_ages(std::move(ages)), m_fresh(std::move(fresh)), m_cutoffDepthRatio(cutoffDepthRatio) {
    if (!m_ages) {
        throw std::invalid_argument("Freshness degradation needs an age model");
    }
    if (!m_fresh) {
        throw std::invalid_argument("Freshness degradation needs the fresh crater morphometry");
    }
    if (!(m_cutoffDepthRatio > 0.0 && m_cutoffDepthRatio < 0.1)) {
        throw std::invalid_argument("Cutoff depth/diameter ratio must be in (0, 0.1)");
    }
}

CraterState FreshnessDegradation::State(const Crater& crater) const {
    const CraterMorphometry fresh = m_fresh(crater.diameter);
    const double floor = std::fmin(m_cutoffDepthRatio / fresh.depthRatio, 1.0);
    const double erosion =
        floor + (1.0 - floor) * m_ages->RemainingLifetime(crater.diameter, crater.ageGyr);
    return {fresh, {erosion * fresh.depthRatio, erosion * fresh.rimHeightRatio}};
}

}  // namespace terraingen
