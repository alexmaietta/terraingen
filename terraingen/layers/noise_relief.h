#pragma once

#include "terraingen/layers/layer.h"
#include "terraingen/sources/synthetic_terrain.h"
#include "terraingen/util/geometry.h"

namespace terraingen {

/// A stack of noise layers added to the terrain: rolling relief on a flat source, or detail on a
/// DEM. Each cell gets `EvaluateNoiseLayers` at its centre, measured from `origin` so that
/// single-precision noise stays accurate in projections whose coordinates are large.
class NoiseRelief final : public Layer {
  public:
    /// @throws std::invalid_argument if `params` are invalid.
    explicit NoiseRelief(SyntheticTerrainParams params, Vec2d origin = {});

    void Apply(Raster<float>& heights, double spacing, TerrainFeatures& features) const override;

    const SyntheticTerrainParams& Params() const {
        return params_;
    }

  private:
    SyntheticTerrainParams params_;
    Vec2d origin_;
};

}  // namespace terraingen
