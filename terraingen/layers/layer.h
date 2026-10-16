/// @file
/// @brief `Layer`, relief added on top of a source, and what layers place besides heights.
#pragma once

#include "terraingen/layers/boulders/boulder.h"
#include "terraingen/layers/boulders/boulder_meshes.h"
#include "terraingen/layers/craters/attributes.h"
#include "terraingen/layers/craters/population.h"
#include "terraingen/layers/ground.h"
#include "terraingen/layers/sample_block.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/surface.h"

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

/// Relief added on top of a source: noise, craters, or a layer of your own.
///
/// A layer implements one method, `ApplySamples`, over a `SampleBlock`: a grid's cells or
/// scattered points. Grids, tiles and point queries all go through it, so a point gets the same
/// height as a grid cell centred there. Implementations must be deterministic and thread-safe, and
/// place the same features in a tile as in any larger grid containing it. A layer needing another
/// layer's features holds a reference to it and queries it (e.g. `CraterLayer::Craters`).
class Layer {
  public:
    virtual ~Layer() = default;

    /// Adds the layer's relief to the cells of `heights` that hold data.
    void Apply(Raster<float>& heights, const LayerContext& context,
               TerrainFeatures& features) const;

    /// Adds the layer's relief to every sample of `samples` that holds data.
    /// @throws std::invalid_argument for points without a positive `context.spacing`.
    void Apply(SampleBlock& samples, const LayerContext& context, TerrainFeatures& features) const;

    /// The height at one point after this layer, from `height` before it.
    /// @throws std::invalid_argument without a positive `context.spacing`.
    float ApplyAt(float height, double x, double y, const LayerContext& context) const;

    /// The largest feature the layer places, in metres, for tiles' and filters' margins.
    virtual double MaxFeatureDiameter() const {
        return 0.0;
    }

  protected:
    /// Adds the layer's relief to `samples`, recording what it places in `features`. A grid's
    /// spacing is `context.spacing` raised to the grid's own; points come with a positive one.
    virtual void ApplySamples(SampleBlock& samples, const LayerContext& context,
                              TerrainFeatures& features) const = 0;
};

/// A layer whose height at a point depends only on the height there and the point: noise,
/// roughness, transforms. Subclasses write `HeightAt`; the base applies it to every sample that
/// holds data, in parallel over a grid's rows.
class FormulaLayer : public Layer {
  protected:
    /// @param surface What the layer's coordinates lie on, which converts a grid's cell size to
    /// metres for the sample spacing.
    explicit FormulaLayer(Surface surface = {}) : surface_(surface) {}

    /// The height at (x, y) after the layer, from `height` before it.
    /// @param spacing The sample spacing in metres.
    /// @param ground The terrain beneath the layer, null when the context gives none.
    virtual float HeightAt(float height, double x, double y, double spacing,
                           const Ground* ground) const = 0;

    /// Checks the context before any sample is evaluated; accepts any by default.
    virtual void CheckContext(const LayerContext& /*context*/) const {}

    void ApplySamples(SampleBlock& samples, const LayerContext& context,
                      TerrainFeatures& features) const final;

    Surface surface_;  ///< What the layer's coordinates lie on.
};

}  // namespace terraingen
