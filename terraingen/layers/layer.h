#pragma once

#include "terraingen/layers/boulders/boulder.h"
#include "terraingen/layers/boulders/boulder_meshes.h"
#include "terraingen/layers/craters/attributes.h"
#include "terraingen/layers/craters/population.h"
#include "terraingen/layers/ground.h"
#include "terraingen/raster/geo_transform.h"
#include "terraingen/raster/raster.h"

#include <cmath>
#include <stdexcept>
#include <vector>

namespace terraingen {

/// What layers place besides heights, for the filters and outputs that follow them.
struct TerrainFeatures {
    CraterList craters;  ///< Every crater the layers applied, in the order they applied them.
    /// The attributes of `craters`, in the same order; layers that record craters record these too.
    std::vector<CraterAttributes> craterAttributes;
    std::vector<Boulder>
        boulders;  ///< Every boulder the layers placed, in the order they placed them.
    /// The boulders placed as 3D models of their own (`Boulder::model`), with their shapes and
    /// placements.
    std::vector<BoulderModel> boulderModels;
};

/// How a layer is evaluated.
struct LayerContext {
    /// Sample spacing in world units: features that cannot be resolved at it are left out. In
    /// `Layer::Apply` it is at least the grid's cell size; in `Layer::ApplyAt` it must be positive.
    double spacing = 0.0;
    /// Threads the layer may split its work over; 0 for every hardware thread. The result does not
    /// depend on it.
    unsigned threads = 1;
    /// The terrain beneath the layer, for layers that read it; may be null where none is needed.
    const Ground* ground = nullptr;
};

/// Relief added on top of a source: noise, craters, or a layer of your own.
///
/// A layer must be deterministic and safe to call from several threads at once, and must place the
/// same features in a tile as in any larger grid containing it, so that a terrain evaluated tile by
/// tile equals the terrain evaluated whole. A layer that needs another layer's features, such as
/// boulders around fresh craters, holds a reference to that layer and queries it (for example
/// `CraterLayer::Craters`), so its point queries see the same features as its grids.
class Layer {
  public:
    virtual ~Layer() = default;

    /// Adds the layer's relief to the cells of `heights` that hold data, and records the features
    /// it placed in `features`.
    virtual void Apply(Raster<float>& heights, const LayerContext& context,
                       TerrainFeatures& features) const = 0;

    /// The height at (`x`, `y`) after this layer, given `height` there before it: what `Apply`
    /// makes of a cell centred there, to the same bits. The default applies the layer to one cell
    /// of side `context.spacing`; layers override it with something faster where they can.
    /// @throws std::invalid_argument if `context.spacing` is not positive and finite.
    virtual float ApplyAt(float height, double x, double y, const LayerContext& context) const {
        if (!(std::isfinite(context.spacing) && context.spacing > 0.0)) {
            throw std::invalid_argument("Sample spacing must be positive and finite");
        }
        Raster<float> cell(1, 1, height);
        cell.SetTransform(GeoTransform::NorthUp(x - 0.5 * context.spacing,
                                                y + 0.5 * context.spacing, context.spacing));
        TerrainFeatures features;
        Apply(cell, context, features);
        return cell(0, 0);
    }

    /// The largest feature the layer places, in world units, for filters' margins; 0 for none.
    virtual double MaxFeatureDiameter() const {
        return 0.0;
    }
};

}  // namespace terraingen
