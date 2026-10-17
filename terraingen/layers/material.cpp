#include "terraingen/layers/material.h"

#include <algorithm>
#include <cstddef>

namespace terraingen {

MaterialShares SoilShares() {
    MaterialShares shares{};
    shares[static_cast<std::size_t>(MaterialClass::Soil)] = 1.0;
    return shares;
}

void CoverWith(MaterialShares& shares, MaterialClass material, double cover) {
    const double weight = std::clamp(cover, 0.0, 1.0);
    for (double& share : shares) {
        share *= 1.0 - weight;
    }
    shares[static_cast<std::size_t>(material)] += weight;
}

}  // namespace terraingen
