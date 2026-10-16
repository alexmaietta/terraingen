#include "terraingen/layers/craters/crater_placer.h"

namespace terraingen {

void CraterFeatures::Append(const CraterFeatures& other) {
    craters.Append(other.craters);
    attributes.insert(attributes.end(), other.attributes.begin(), other.attributes.end());
}

CraterFeatures PlacedCraters(std::span<const std::shared_ptr<const Layer>> layers,
                             const RasterGrid& grid, double spacing) {
    CraterFeatures placed;
    for (const auto& layer : layers) {
        if (const auto* placer = dynamic_cast<const CraterPlacer*>(layer.get())) {
            placed.Append(placer->CratersOver(grid, spacing));
        }
    }
    return placed;
}

}  // namespace terraingen
