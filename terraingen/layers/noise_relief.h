/// @file
/// @brief `NoiseRelief`, a stack of noise layers added to the terrain, and `NoiseLayersAt`.
#pragma once

#include "terraingen/layers/kernels/noise_relief.h"
#include "terraingen/layers/layer.h"
#include "terraingen/noise/noise_layers.h"
#include "terraingen/sources/synthetic_terrain.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <vector>

namespace terraingen {

/// The summed noise of `layers` at surface (x, y): 2D relative to `origin` on a plane (keeping
/// single precision accurate), 3D at the point's 3D position on a sphere.
/// @param origin Where plane coordinates are measured from; unused on a sphere.
float NoiseLayersAt(const std::vector<NoiseLayer>& layers, const Vec2d& origin,
                    const Surface& surface, double x, double y);

/// A stack of noise layers added to the terrain: rolling relief on a flat source, or detail on a
/// DEM. Each sample gets `NoiseLayersAt`, seamless on a sphere.
///
/// On a plane the relief is a kernel, so grids follow the context's backend; on a sphere it
/// is evaluated in 3D on the CPU.
class NoiseRelief final : public Layer {
  public:
    /// @param origin Where plane coordinates are measured from; unused on a sphere.
    /// @throws std::invalid_argument if `params` are invalid.
    explicit NoiseRelief(SyntheticTerrainParams params, Vec2d origin = {}, Surface surface = {});

    /// The noise layers the layer was made with.
    const SyntheticTerrainParams& Params() const {
        return params_;
    }

  protected:
    void ApplySamples(SampleBlock& samples, const LayerContext& context) const override;

  private:
    SyntheticTerrainParams params_;
    Vec2d origin_;
    Surface surface_;
    NoiseReliefKernel kernel_;
};

}  // namespace terraingen
