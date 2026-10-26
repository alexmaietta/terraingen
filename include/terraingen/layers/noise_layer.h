// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `NoiseLayer`, a stack of fractals added to the terrain.
#pragma once

#include "terraingen/layers/field.h"
#include "terraingen/layers/kernels/noise_layer.h"
#include "terraingen/layers/layer.h"
#include "terraingen/noise/fractals.h"
#include "terraingen/sources/terrain_base.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace terraingen {

/// The noise a `NoiseLayer` adds: its stack of fractals and what of it a source already holds.
struct NoiseLayerOptions {
    std::vector<Fractal> fractals{};  ///< The stack, at least one fractal.
    /// Source cells across from which its measured data already holds a fractal: with a source,
    /// each fractal of the stack whose `wavelength`, its longest, is at least that many of the
    /// source's cells at a point is left out there (`SourceResolvedLimit`); unset adds every
    /// fractal everywhere.
    std::optional<double> sourceResolvesPixels{};
};

/// A stack of fractals added to the terrain, rolling relief on a flat source or detail on a DEM.
/// Grids on a plane follow the context's backend; on a sphere or over a resolving source it runs
/// on the CPU.
class NoiseLayer final : public Layer {
  public:
    /// @throws std::invalid_argument for a stack without fractals or otherwise invalid, or for a
    /// `sourceResolvesPixels` that is not positive and finite.
    explicit NoiseLayer(NoiseLayerOptions options, TerrainBase base = {});

    /// Noise of the stack `fractals`.
    /// @throws std::invalid_argument as the options constructor.
    explicit NoiseLayer(std::vector<Fractal> fractals, TerrainBase base = {})
        : NoiseLayer(NoiseLayerOptions{std::move(fractals), std::nullopt}, std::move(base)) {}

    /// Noise of the one fractal `fractal`.
    /// @throws std::invalid_argument as the options constructor.
    explicit NoiseLayer(Fractal fractal, TerrainBase base = {})
        : NoiseLayer(std::vector<Fractal>{fractal}, std::move(base)) {}

    /// The fractals and source limit the layer was made with.
    const NoiseLayerOptions& Options() const {
        return options_;
    }

    std::optional<Surface> LayerSurface() const override {
        return surface_;
    }

  protected:
    void ApplySamples(SampleBlock& samples, const LayerContext& context) const override;

  private:
    NoiseLayerOptions options_;
    Vec2d origin_;
    Surface surface_;
    NoiseLayerKernel kernel_;
    std::shared_ptr<const SurfaceField> limit_;  ///< `SourceResolvedLimit`; null for none.
    std::vector<double> wavelengths_;  ///< The fractals' distinct wavelengths in meters, ascending.
    /// The stack with the fractals of `wavelengths_[i]` and longer disabled, the whole stack last.
    std::vector<std::vector<Fractal>> stacks_;
};

}  // namespace terraingen
