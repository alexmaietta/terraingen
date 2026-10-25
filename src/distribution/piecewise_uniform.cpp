// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/distribution/piecewise_uniform.h"

#include "util/checks.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

/// The diameters the bands cover, up to the largest `max`, after checking the bands.
DiameterRange BandRange(const std::vector<UniformBand>& bands) {
    if (bands.empty()) {
        throw std::invalid_argument("A piecewise uniform distribution needs at least one band");
    }
    double top = 0.0;
    for (const UniformBand& band : bands) {
        if (!IsPositive(band.min) || !IsPositive(band.max) || !(band.max > band.min)) {
            throw std::invalid_argument(
                "Band diameters must be positive and finite, with max above min");
        }
        if (!std::isfinite(band.density) || band.density < 0.0) {
            throw std::invalid_argument("Band densities must be finite and >= 0");
        }
        top = std::max(top, band.max);
    }
    return DiameterRange{0.0, top};
}

/// `bands` with densities scaled to sum to `total` when it is positive.
std::vector<UniformBand> ScaleBands(std::vector<UniformBand> bands, double total) {
    if (!std::isfinite(total) || total < 0.0) {
        throw std::invalid_argument("A band total must be finite and >= 0");
    }
    double sum = 0.0;
    for (const UniformBand& band : bands) {
        sum += band.density;
    }
    if (!(sum > 0.0)) {
        throw std::invalid_argument("A piecewise uniform distribution needs a positive density");
    }
    if (total > 0.0) {
        for (UniformBand& band : bands) {
            band.density *= total / sum;
        }
    }
    return bands;
}

}  // namespace

PiecewiseUniformSfd::PiecewiseUniformSfd(std::vector<UniformBand> bands, SfdKind kind, double total)
    : SizeFrequencyDistribution(kind, BandRange(bands)),
      bands_(ScaleBands(std::move(bands), total)) {
    SetAllowExtrapolation(true);
    for (const UniformBand& band : bands_) {
        edges_.push_back(band.min);
        edges_.push_back(band.max);
    }
    std::sort(edges_.begin(), edges_.end());
    edges_.erase(std::unique(edges_.begin(), edges_.end()), edges_.end());
    for (const double edge : edges_) {
        edgeCumulative_.push_back(CumulativeAt(edge));
    }
}

double PiecewiseUniformSfd::CumulativeAt(double diameter) const {
    double total = 0.0;
    for (const UniformBand& band : bands_) {
        total += band.density * std::clamp((band.max - diameter) / (band.max - band.min), 0.0, 1.0);
    }
    return total;
}

double PiecewiseUniformSfd::IncrementalAt(double diameter) const {
    double total = 0.0;
    for (const UniformBand& band : bands_) {
        if (diameter >= band.min && diameter < band.max) {
            total += band.density / (band.max - band.min);
        }
    }
    return total;
}

double PiecewiseUniformSfd::InvertCumulative(double target, double /*dMin*/,
                                             double /*dMax*/) const {
    const auto below = std::partition_point(edgeCumulative_.begin(), edgeCumulative_.end(),
                                            [target](double value) { return value > target; });
    const auto i = static_cast<std::size_t>(below - edgeCumulative_.begin());
    if (i == 0) {
        return edges_.front();
    }
    if (i == edges_.size()) {
        return edges_.back();
    }
    const double high = edgeCumulative_[i - 1];
    const double low = edgeCumulative_[i];
    return edges_[i - 1] + (high - target) / (high - low) * (edges_[i] - edges_[i - 1]);
}

}  // namespace terraingen
