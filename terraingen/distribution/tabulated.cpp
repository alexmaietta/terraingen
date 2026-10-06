#include "terraingen/distribution/tabulated.h"

#include "terraingen/distribution/checks.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

DiameterRange TableRange(const std::vector<double>& diameters,
                         const std::vector<double>& cumulative, double diameterUnit,
                         double areaUnit) {
    if (diameters.size() < 2 || diameters.size() != cumulative.size()) {
        throw std::invalid_argument("A table needs at least two diameters with one value each");
    }
    if (!detail::PositiveFinite(diameterUnit) || !detail::PositiveFinite(areaUnit)) {
        throw std::invalid_argument("Diameter and area units must be positive and finite");
    }
    for (std::size_t i = 0; i < diameters.size(); ++i) {
        if (!detail::PositiveFinite(diameters[i]) || !detail::PositiveFinite(cumulative[i])) {
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
      diameters_(std::move(diameters)), cumulative_(std::move(cumulative)) {
    for (std::size_t i = 0; i < diameters_.size(); ++i) {
        diameters_[i] *= diameterUnit;
        cumulative_[i] /= areaUnit;
        if (i > 0) {
            slopes_.push_back(std::log(cumulative_[i] / cumulative_[i - 1]) /
                              std::log(diameters_[i] / diameters_[i - 1]));
        }
    }
}

std::size_t TabulatedSfd::IntervalAt(double diameter) const {
    const auto upper = std::upper_bound(diameters_.begin(), diameters_.end(), diameter);
    const auto index = static_cast<std::size_t>(upper - diameters_.begin());
    return std::clamp<std::size_t>(index, 1, diameters_.size() - 1) - 1;
}

double TabulatedSfd::CumulativeAt(double diameter) const {
    const std::size_t i = IntervalAt(diameter);
    return cumulative_[i] * std::pow(diameter / diameters_[i], slopes_[i]);
}

double TabulatedSfd::IncrementalAt(double diameter) const {
    return -slopes_[IntervalAt(diameter)] * CumulativeAt(diameter) / diameter;
}

double TabulatedSfd::InvertCumulative(double target, double /*dMin*/, double /*dMax*/) const {
    const auto above = std::partition_point(cumulative_.begin(), cumulative_.end(),
                                            [target](double value) { return value > target; });
    const auto index = static_cast<std::size_t>(above - cumulative_.begin());
    const std::size_t i = std::clamp<std::size_t>(index, 1, cumulative_.size() - 1) - 1;
    return diameters_[i] * std::pow(target / cumulative_[i], 1.0 / slopes_[i]);
}

}  // namespace terraingen
