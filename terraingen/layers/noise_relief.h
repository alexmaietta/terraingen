#pragma once

#include "terraingen/layers/layer.h"
#include "terraingen/sources/synthetic_terrain.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

namespace terraingen {

/// A stack of noise layers added to the terrain: rolling relief on a flat source, or detail on a
/// DEM. Each cell gets `EvaluateNoiseLayers` at its centre, measured from `origin` so that
/// single-precision noise stays accurate in projections whose coordinates are large. On a sphere,
/// each cell gets `EvaluateNoiseLayers3` at its 3D point (`Surface::Position`), seamless across
/// the antimeridian and the poles, and `origin` is unused.
class NoiseRelief final : public Layer {
  public:
    /// @throws std::invalid_argument if `params` are invalid, or on a sphere use a basis without a
    /// 3D form (`HasBasisNoise3`).
    explicit NoiseRelief(SyntheticTerrainParams params, Vec2d origin = {}, Surface surface = {});

    void Apply(Raster<float>& heights, const LayerContext& context,
               TerrainFeatures& features) const override;

    /// Noise added at (`x`, `y`); NaN stays NaN.
    float ApplyAt(float height, double x, double y, const LayerContext& context) const override;

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
