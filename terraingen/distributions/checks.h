#pragma once

#include <cmath>

namespace terraingen::detail {

inline bool PositiveFinite(double value) {
    return std::isfinite(value) && value > 0.0;
}

}  // namespace terraingen::detail
