// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/transforms.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

ScaleLayer::ScaleLayer(double scale, double offset) : scale_(scale), offset_(offset) {
    if (!(std::isfinite(scale) && std::isfinite(offset))) {
        throw std::invalid_argument("Scale and offset must be finite");
    }
}

float ScaleLayer::HeightAt(float height, double /*x*/, double /*y*/,
                           const LayerContext& /*context*/) const {
    return static_cast<float>(static_cast<double>(height) * scale_ + offset_);
}

ClampLayer::ClampLayer(std::optional<double> min, std::optional<double> max)
    : min_(min), max_(max) {
    if ((min && !std::isfinite(*min)) || (max && !std::isfinite(*max))) {
        throw std::invalid_argument("Clamp bounds must be finite");
    }
    if (min && max && *min > *max) {
        throw std::invalid_argument("Clamp min must not exceed max");
    }
}

float ClampLayer::HeightAt(float height, double /*x*/, double /*y*/,
                           const LayerContext& /*context*/) const {
    double value = static_cast<double>(height);
    if (min_) {
        value = std::max(value, *min_);
    }
    if (max_) {
        value = std::min(value, *max_);
    }
    return static_cast<float>(value);
}

FunctionLayer::FunctionLayer(HeightFunction function) : function_(std::move(function)) {
    if (!function_) {
        throw std::invalid_argument("A function layer needs a function");
    }
}

float FunctionLayer::HeightAt(float height, double x, double y, const LayerContext& context) const {
    return static_cast<float>(function_(x, y, context.spacing, static_cast<double>(height)));
}

}  // namespace terraingen
