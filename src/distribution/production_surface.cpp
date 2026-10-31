// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/distribution/production_surface.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

ProductionSurface::ProductionSurface(std::shared_ptr<const SizeFrequencyDistribution> production,
                                     double ageGyr, const Chronology& chronology)
    : SizeFrequencyDistribution(SfdKind::Density,
                                production ? production->ValidRange() : DiameterRange{}),
      m_production(std::move(production)), m_ageGyr(ageGyr) {
    if (!m_production || m_production->Kind() != SfdKind::ProductionRate) {
        throw std::invalid_argument("A production surface needs a production rate distribution");
    }
    if (!chronology) {
        throw std::invalid_argument("A production surface needs a chronology");
    }
    SetAllowExtrapolation(m_production->AllowsExtrapolation());
    m_scale = chronology(m_ageGyr) / m_production->Cumulative(1000.0);
}

double ProductionSurface::CumulativeAt(double diameter) const {
    return m_scale * UncheckedCumulative(*m_production, diameter);
}

double ProductionSurface::IncrementalAt(double diameter) const {
    return m_scale * UncheckedIncremental(*m_production, diameter);
}

double ProductionSurface::InvertCumulative(double target, double minDiameter,
                                           double maxDiameter) const {
    return UncheckedInverse(*m_production, target / m_scale, minDiameter, maxDiameter);
}

namespace detail {

namespace {

/// Square meters per square kilometer.
constexpr double kSquareKilometer = 1e6;

void CheckAge(const ExponentialChronology& chronology, double ageGyr) {
    if (!(ageGyr >= 0.0 && ageGyr <= chronology.maxAgeGyr)) {
        throw std::domain_error(chronology.ageError);
    }
}

}  // namespace

double ExponentialChronology::Count(double ageGyr) const {
    CheckAge(*this, ageGyr);
    return (burst * std::expm1(decay * ageGyr) + linear * ageGyr) / kSquareKilometer;
}

double ExponentialChronology::Rate(double ageGyr) const {
    CheckAge(*this, ageGyr);
    return (burst * decay * std::exp(decay * ageGyr) + linear) / kSquareKilometer;
}

}  // namespace detail

}  // namespace terraingen
