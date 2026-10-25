// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `NoiseRelief`, a stack of noise layers added to the terrain.
#pragma once

#include "terraingen/layers/kernels/noise_relief.h"
#include "terraingen/layers/layer.h"
#include "terraingen/noise/noise_layers.h"
#include "terraingen/sources/field.h"
#include "terraingen/sources/terrain_base.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace terraingen {

/// The noise a `NoiseRelief` adds: its stack of layers and what of it a source already holds.
struct NoiseReliefOptions {
    std::vector<NoiseLayer> layers{};  ///< The stack, at least one layer.
    /// Source cells across from which its measured data already holds a noise layer: with a
    /// source, each layer of the stack whose `wavelength`, its longest, is at least that many of
    /// the source's cells at a point is left out there (`SourceResolvedLimit`); unset adds every
    /// layer everywhere.
    std::optional<double> sourceResolvesPixels{};
};

/// A stack of noise layers added to the terrain: rolling relief on a flat source, or detail on a
/// DEM. Each sample gets `NoiseLayersAt`, seamless on a sphere, measured on a plane from the
/// base's reference point.
///
/// On a plane the relief is a kernel, so grids follow the context's backend; on a sphere, or
/// with a source that resolves some of the layers, it is evaluated on the CPU.
class NoiseRelief final : public Layer {
  public:
    /// @throws std::invalid_argument for a stack without layers or otherwise invalid, or for a
    /// `sourceResolvesPixels` that is not positive and finite.
    explicit NoiseRelief(NoiseReliefOptions options, TerrainBase base = {});

    /// Relief of the stack `layers`.
    /// @throws std::invalid_argument as the options constructor.
    explicit NoiseRelief(std::vector<NoiseLayer> layers, TerrainBase base = {})
        : NoiseRelief(NoiseReliefOptions{std::move(layers), std::nullopt}, std::move(base)) {}

    /// Relief of the one noise layer `layer`.
    /// @throws std::invalid_argument as the options constructor.
    explicit NoiseRelief(NoiseLayer layer, TerrainBase base = {})
        : NoiseRelief(std::vector<NoiseLayer>{layer}, std::move(base)) {}

    /// The noise layers and source limit the layer was made with.
    const NoiseReliefOptions& Options() const {
        return options_;
    }

    std::optional<Surface> LayerSurface() const override {
        return surface_;
    }

  protected:
    void ApplySamples(SampleBlock& samples, const LayerContext& context) const override;

  private:
    NoiseReliefOptions options_;
    Vec2d origin_;
    Surface surface_;
    NoiseReliefKernel kernel_;
    std::shared_ptr<const SurfaceField> limit_;  ///< `SourceResolvedLimit`; null for none.
    std::vector<double> wavelengths_;  ///< The layers' distinct wavelengths in meters, ascending.
    /// The stack with the layers of `wavelengths_[i]` and longer disabled, the whole stack last.
    std::vector<std::vector<NoiseLayer>> stacks_;
};

}  // namespace terraingen
