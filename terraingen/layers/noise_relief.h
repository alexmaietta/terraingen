/// @file
/// @brief `NoiseRelief`, a stack of noise layers added to the terrain.
#pragma once

#include "terraingen/layers/kernels/noise_relief.h"
#include "terraingen/layers/layer.h"
#include "terraingen/noise/noise_layers.h"
#include "terraingen/sources/field.h"
#include "terraingen/sources/source.h"
#include "terraingen/sources/synthetic_source.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <memory>
#include <optional>
#include <vector>

namespace terraingen {

/// A stack of noise layers added to the terrain: rolling relief on a flat source, or detail on a
/// DEM. Each sample gets `NoiseLayersAt`, seamless on a sphere.
///
/// On a plane the relief is a kernel, so grids follow the context's backend; on a sphere, or
/// with a source that resolves some of the layers, it is evaluated on the CPU.
class NoiseRelief final : public Layer {
  public:
    /// @param origin Where plane coordinates are measured from; unused on a sphere.
    /// @param source The source the terrain's heights come from; null for none.
    /// @param sourceResolvesPixels Source cells across from which its measured data already holds
    /// a noise layer: with a source, each layer of the stack whose `wavelength`, its longest, is
    /// at least that many of the source's cells at a point is left out there
    /// (`SourceResolvedLimit`); unset adds every layer everywhere.
    /// @throws std::invalid_argument for a stack without layers or otherwise invalid, or for a
    /// `sourceResolvesPixels` that is not positive and finite.
    explicit NoiseRelief(SyntheticTerrainParams params, Vec2d origin = {}, Surface surface = {},
                         std::shared_ptr<const Source> source = nullptr,
                         std::optional<double> sourceResolvesPixels = std::nullopt);

    /// The noise layers the layer was made with.
    const SyntheticTerrainParams& Params() const {
        return params_;
    }

    /// The source cells across from which the source holds a noise layer; unset for none.
    const std::optional<double>& SourceResolvesPixels() const {
        return sourceResolvesPixels_;
    }

  protected:
    void ApplySamples(SampleBlock& samples, const LayerContext& context) const override;

  private:
    SyntheticTerrainParams params_;
    Vec2d origin_;
    Surface surface_;
    std::optional<double> sourceResolvesPixels_;
    NoiseReliefKernel kernel_;
    std::shared_ptr<const SurfaceField> limit_;  ///< `SourceResolvedLimit`; null for none.
    std::vector<double> wavelengths_;  ///< The layers' distinct wavelengths in meters, ascending.
    /// The stack with the layers of `wavelengths_[i]` and longer disabled, the whole stack last.
    std::vector<std::vector<NoiseLayer>> stacks_;
};

}  // namespace terraingen
