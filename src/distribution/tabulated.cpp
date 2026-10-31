// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/distribution/tabulated.h"

#include "util/checks.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

/// The table's diameter range in meters, after checking the table and units.
DiameterRange TableRange(const std::vector<double>& diameters,
                         const std::vector<double>& cumulative, double diameterUnit,
                         double areaUnit) {
    if (diameters.size() < 2 || diameters.size() != cumulative.size()) {
        throw std::invalid_argument("A table needs at least two diameters with one value each");
    }
    if (!IsPositive(diameterUnit) || !IsPositive(areaUnit)) {
        throw std::invalid_argument("Diameter and area units must be positive and finite");
    }
    for (std::size_t i = 0; i < diameters.size(); ++i) {
        if (!IsPositive(diameters[i]) || !IsPositive(cumulative[i])) {
            throw std::invalid_argument("Table diameters and values must be positive and finite");
        }
        if (i > 0 && !(diameters[i] > diameters[i - 1])) {
            throw std::invalid_argument("Table diameters must strictly increase");
        }
        if (i > 0 && cumulative[i] > cumulative[i - 1]) {
            throw std::invalid_argument("A cumulative distribution cannot increase with diameter");
        }
    }
    return DiameterRange{diameters.front() * diameterUnit, diameters.back() * diameterUnit};
}

}  // namespace

TabulatedSfd::TabulatedSfd(std::vector<double> diameters, std::vector<double> cumulative,
                           SfdKind kind, double diameterUnit, double areaUnit)
    : SizeFrequencyDistribution(kind, TableRange(diameters, cumulative, diameterUnit, areaUnit)),
      m_diameters(std::move(diameters)), m_cumulative(std::move(cumulative)) {
    for (std::size_t i = 0; i < m_diameters.size(); ++i) {
        m_diameters[i] *= diameterUnit;
        m_cumulative[i] /= areaUnit;
        if (i > 0) {
            m_slopes.push_back(std::log(m_cumulative[i] / m_cumulative[i - 1]) /
                               std::log(m_diameters[i] / m_diameters[i - 1]));
        }
    }
}

std::size_t TabulatedSfd::IntervalAt(double diameter) const {
    const auto upper = std::upper_bound(m_diameters.begin(), m_diameters.end(), diameter);
    const auto index = static_cast<std::size_t>(upper - m_diameters.begin());
    return std::clamp<std::size_t>(index, 1, m_diameters.size() - 1) - 1;
}

double TabulatedSfd::CumulativeAt(double diameter) const {
    const std::size_t i = IntervalAt(diameter);
    return m_cumulative[i] * std::pow(diameter / m_diameters[i], m_slopes[i]);
}

double TabulatedSfd::IncrementalAt(double diameter) const {
    return -m_slopes[IntervalAt(diameter)] * CumulativeAt(diameter) / diameter;
}

double TabulatedSfd::InvertCumulative(double target, double /*minDiameter*/,
                                      double /*maxDiameter*/) const {
    const auto above = std::partition_point(m_cumulative.begin(), m_cumulative.end(),
                                            [target](double value) { return value > target; });
    const auto index = static_cast<std::size_t>(above - m_cumulative.begin());
    const std::size_t i = std::clamp<std::size_t>(index, 1, m_cumulative.size() - 1) - 1;
    if (m_slopes[i] == 0.0) {
        return m_diameters[i];
    }
    return m_diameters[i] * std::pow(target / m_cumulative[i], 1.0 / m_slopes[i]);
}

}  // namespace terraingen
