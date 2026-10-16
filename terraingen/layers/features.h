/// @file
/// @brief `TerrainFeatures`, what layers place besides heights.
#pragma once

#include "terraingen/layers/boulders/boulder.h"
#include "terraingen/layers/boulders/boulder_models.h"
#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/craters/shaping/attributes.h"

#include <cstddef>
#include <vector>

namespace terraingen {

/// @brief What layers place besides heights, for the filters and outputs that follow them.
struct TerrainFeatures {
    CraterList craters;  ///< Every crater the layers applied, in the order they applied them.
    std::vector<CraterAttributes> craterAttributes;  ///< Attributes of `craters`, in step with it.
    std::vector<Boulder> boulders;  ///< Every boulder the layers placed, in the order placed.
    std::vector<BoulderModel> boulderModels;  ///< Boulders placed as their own 3D models.

    /// @brief Records craters with their attributes, keeping the two lists in step.
    /// @param added The craters, in the order applied.
    /// @param attributesOf `attributesOf(crater)` gives a crater's `CraterAttributes`.
    template <typename AttributesOf>
    void AddCraters(const CraterList& added, const AttributesOf& attributesOf) {
        for (std::size_t n = 0; n < added.Size(); ++n) {
            craterAttributes.push_back(attributesOf(added.At(n)));
        }
        craters.Append(added);
    }
};

}  // namespace terraingen
