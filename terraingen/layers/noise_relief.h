/// @file
/// @brief `NoiseRelief`, a stack of noise layers added to the terrain, and `NoiseLayersAt`, such a
/// stack read on a plane or a sphere.
#pragma once

#include "terraingen/layers/layer.h"
#include "terraingen/noise/noise_layers.h"
#include "terraingen/sources/synthetic_terrain.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <vector>

namespace terraingen {

/// @brief A stack of noise layers at a point: `EvaluateNoiseLayers` relative to `origin` on a
/// plane (keeping single precision accurate), `EvaluateNoiseLayers3` at the 3D point on a sphere.
/// @param layers The noise layers; on a sphere each needs a basis with a 3D form.
/// @param origin Where plane coordinates are measured from; unused on a sphere.
/// @param surface What the point lies on.
/// @param x Surface x of the point.
/// @param y Surface y of the point.
/// @return The summed noise.
float NoiseLayersAt(const std::vector<NoiseLayer>& layers, const Vec2d& origin,
                    const Surface& surface, double x, double y);

/// @brief A stack of noise layers added to the terrain: rolling relief on a flat source, or detail
/// on a DEM.
///
/// Each cell gets `NoiseLayersAt` at its centre: 2D noise relative to `origin` on a plane, 3D
/// noise at the cell's 3D point on a sphere (seamless across the antimeridian and poles).
class NoiseRelief final : public FormulaLayer {
  public:
    /// @param origin Where plane coordinates are measured from; unused on a sphere.
    /// @throws std::invalid_argument if `params` are invalid, or on a sphere use a basis without a
    /// 3D form (`HasBasisNoise3`).
    explicit NoiseRelief(SyntheticTerrainParams params, Vec2d origin = {}, Surface surface = {});

    /// The noise layers the layer was made with.
    const SyntheticTerrainParams& Params() const {
        return params_;
    }

  protected:
    float HeightAt(float height, double x, double y, double spacing,
                   const Ground* ground) const override;

  private:
    SyntheticTerrainParams params_;
    Vec2d origin_;
};

}  // namespace terraingen
