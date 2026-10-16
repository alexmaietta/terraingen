/// @file
/// @brief `NoiseRelief`, a stack of noise layers added to the terrain.
#pragma once

#include "terraingen/layers/layer.h"
#include "terraingen/sources/synthetic_terrain.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

namespace terraingen {

/// @brief A stack of noise layers added to the terrain: rolling relief on a flat source, or detail
/// on a DEM.
///
/// Each cell gets `NoiseLayersAt` at its centre: 2D noise relative to `origin` on a plane, 3D
/// noise at the cell's 3D point on a sphere (seamless across the antimeridian and poles).
class NoiseRelief final : public Layer {
  public:
    /// @brief Makes the layer.
    /// @param params The noise layers.
    /// @param origin Where plane coordinates are measured from; unused on a sphere.
    /// @param surface What the terrain lies on.
    /// @throws std::invalid_argument if `params` are invalid, or on a sphere use a basis without a
    /// 3D form (`HasBasisNoise3`).
    explicit NoiseRelief(SyntheticTerrainParams params, Vec2d origin = {}, Surface surface = {});

    /// @brief Adds the noise to every cell of `heights`.
    /// @param heights The heights to change in place.
    /// @param context How the layer is evaluated.
    /// @param features Unused; noise places no features.
    void Apply(Raster<float>& heights, const LayerContext& context,
               TerrainFeatures& features) const override;

    /// @brief The height at a point plus the noise there.
    /// @param height The height before the layer; NaN stays NaN.
    /// @param x World x of the point.
    /// @param y World y of the point.
    /// @param context Unused.
    /// @return The height after the layer.
    float ApplyAt(float height, double x, double y, const LayerContext& context) const override;

    /// @brief The noise layers.
    /// @return The parameters the layer was made with.
    const SyntheticTerrainParams& Params() const {
        return params_;
    }

  private:
    float NoiseAt(double x, double y) const;

    SyntheticTerrainParams params_;
    Vec2d origin_;
    Surface surface_;
};

}  // namespace terraingen
