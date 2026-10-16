/// @file
/// @brief `Layer`, relief added on top of a source, and what layers place besides heights.
#pragma once

#include "terraingen/layers/boulders/boulder.h"
#include "terraingen/layers/boulders/boulder_meshes.h"
#include "terraingen/layers/craters/attributes.h"
#include "terraingen/layers/craters/population.h"
#include "terraingen/layers/ground.h"
#include "terraingen/raster/geo_transform.h"
#include "terraingen/raster/raster.h"

#include <string_view>
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

/// @brief How a layer is evaluated.
struct LayerContext {
    /// Sample spacing in world units; features unresolvable at it are left out. At least the cell
    /// size in `Layer::Apply`; positive in `Layer::ApplyAt`.
    double spacing = 0.0;
    unsigned threads = 1;  ///< Worker threads, 0 for all; the result does not depend on it.
    const Ground* ground = nullptr;  ///< The terrain beneath the layer; null where none is needed.
};

/// @brief Checks the context of a point query (`Layer::ApplyAt`).
/// @param context The context to check.
/// @throws std::invalid_argument if `context.spacing` is not positive and finite.
void ValidatePointContext(const LayerContext& context);

/// @brief Checks that a layer that reads the ground beneath it was given one.
/// @param ground The ground, or null.
/// @param what What reads it, which starts the error message ("<what> need the ground").
/// @throws std::invalid_argument if `ground` is null.
void RequireGround(const Ground* ground, std::string_view what);

/// @brief Relief added on top of a source: noise, craters, or a layer of your own.
///
/// Implementations must be deterministic and thread-safe, and place the same features in a tile as
/// in any larger grid containing it. A layer needing another layer's features holds a reference to
/// it and queries it (e.g. `CraterLayer::Craters`).
class Layer {
  public:
    virtual ~Layer() = default;

    /// @brief Adds the layer's relief to the cells of `heights` that hold data.
    /// @param heights The heights to change in place.
    /// @param context How the layer is evaluated.
    /// @param features Where the features the layer placed are recorded.
    virtual void Apply(Raster<float>& heights, const LayerContext& context,
                       TerrainFeatures& features) const = 0;

    /// @brief The height at a point after this layer, bit-identical to `Apply` on a cell centred
    /// there. The default applies the layer to one cell of side `context.spacing`.
    /// @param height The height there before the layer.
    /// @param x World x of the point.
    /// @param y World y of the point.
    /// @param context How the layer is evaluated.
    /// @return The height after the layer.
    /// @throws std::invalid_argument as `ValidatePointContext`.
    virtual float ApplyAt(float height, double x, double y, const LayerContext& context) const;

    /// @brief The largest feature the layer places, for filters' margins.
    /// @return Its diameter in world units; 0 for none.
    virtual double MaxFeatureDiameter() const {
        return 0.0;
    }
};

}  // namespace terraingen
