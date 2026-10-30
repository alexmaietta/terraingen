// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `NoiseLayer`, a stack of fractals added to the terrain.
#pragma once

#include "terraingen/layers/field.h"
#include "terraingen/layers/kernels/noise_layer.h"
#include "terraingen/layers/layer.h"
#include "terraingen/noise/fractals.h"

#include <cstdint>
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

/// A stack of fractals added to the terrain, rolling relief on a flat source or detail on a DEM,
/// measured from the context's `ReferencePoint`. Grids on a plane follow the context's backend; on
/// a body or over a resolving source it runs on the CPU.
class NoiseLayer final : public Layer {
  public:
    /// @throws std::invalid_argument for a stack without fractals or otherwise invalid, or for a
    /// `sourceResolvesPixels` that is not positive and finite.
    explicit NoiseLayer(NoiseLayerOptions options);

    /// Noise of the stack `fractals`.
    /// @throws std::invalid_argument as the options constructor.
    explicit NoiseLayer(std::vector<Fractal> fractals)
        : NoiseLayer(NoiseLayerOptions{std::move(fractals), std::nullopt}) {}

    /// Noise of the one fractal `fractal`.
    /// @throws std::invalid_argument as the options constructor.
    explicit NoiseLayer(Fractal fractal) : NoiseLayer(std::vector<Fractal>{fractal}) {}

    /// The fractals and source limit the layer was made with.
    const NoiseLayerOptions& Options() const {
        return options_;
    }

    /// The layer with the seed of fractal n `ReseedWord(seed, n)`.
    LayerPtr Reseeded(std::uint64_t seed) const override;

  protected:
    void ApplySamples(SampleBlock& samples, const LayerContext& context) const override;

  private:
    NoiseLayerOptions options_;
    NoiseLayerKernel kernel_;
    std::vector<double> wavelengths_;  ///< The fractals' distinct wavelengths in meters, ascending.
    /// The stack with the fractals of `wavelengths_[i]` and longer disabled, the whole stack last.
    std::vector<std::vector<Fractal>> stacks_;
};

}  // namespace terraingen
