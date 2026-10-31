// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/transforms.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

ScaleLayer::ScaleLayer(double scale, double offset) : m_scale(scale), m_offset(offset) {
    if (!(std::isfinite(scale) && std::isfinite(offset))) {
        throw std::invalid_argument("Scale and offset must be finite");
    }
}

float ScaleLayer::HeightAt(float height, double /*x*/, double /*y*/,
                           const LayerContext& /*context*/) const {
    return static_cast<float>(static_cast<double>(height) * m_scale + m_offset);
}

ClampLayer::ClampLayer(std::optional<double> min, std::optional<double> max)
    : m_min(min), m_max(max) {
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
    if (m_min) {
        value = std::max(value, *m_min);
    }
    if (m_max) {
        value = std::min(value, *m_max);
    }
    return static_cast<float>(value);
}

FunctionLayer::FunctionLayer(HeightFunction function) : m_function(std::move(function)) {
    if (!m_function) {
        throw std::invalid_argument("A function layer needs a function");
    }
}

float FunctionLayer::HeightAt(float height, double x, double y, const LayerContext& context) const {
    return static_cast<float>(m_function(x, y, context.spacing, static_cast<double>(height)));
}

}  // namespace terraingen
