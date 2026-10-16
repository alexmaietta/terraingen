#include "terraingen/layers/detail.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

DetailLayer::DetailLayer(std::shared_ptr<const Source> full, std::shared_ptr<const Source> smooth,
                         double weight, Surface surface)
    : FormulaLayer(surface), full_(std::move(full)), smooth_(std::move(smooth)), weight_(weight) {
    if (!full_ || !smooth_) {
        throw std::invalid_argument("A detail layer needs both sources");
    }
    if (!std::isfinite(weight_)) {
        throw std::invalid_argument("Detail weight must be finite");
    }
}

float DetailLayer::HeightAt(float height, double x, double y, double /*spacing*/,
                            const Ground* /*ground*/) const {
    const float full = full_->HeightAt(x, y);
    const float smooth = smooth_->HeightAt(x, y);
    if (std::isnan(full) || std::isnan(smooth)) {
        return height;
    }
    return static_cast<float>(static_cast<double>(height) +
                              weight_ * (static_cast<double>(full) - static_cast<double>(smooth)));
}

}  // namespace terraingen
