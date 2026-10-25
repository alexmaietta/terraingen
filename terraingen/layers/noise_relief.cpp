// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/noise_relief.h"

#include "terraingen/layers/kernel.h"
#include "terraingen/layers/source_resolved.h"
#include "terraingen/noise/surface_noise.h"
#include "terraingen/noise/validate.h"
#include "terraingen/raster/cell_loops.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace terraingen {

NoiseRelief::NoiseRelief(NoiseReliefOptions options, TerrainBase base)
    : options_(std::move(options)), origin_(base.ReferencePoint()), surface_(base.surface) {
    ValidateNoiseLayers(options_.layers, 1);
    ValidateSourceResolvesPixels(options_.sourceResolvesPixels);
    kernel_ = MakeNoiseReliefKernel(options_.layers);
    limit_ = LayerResolvedLimit(base.source, options_.sourceResolvesPixels, surface_);
    if (!limit_) {
        return;
    }
    for (const NoiseLayer& layer : options_.layers) {
        wavelengths_.push_back(static_cast<double>(layer.fractal.wavelength));
    }
    std::sort(wavelengths_.begin(), wavelengths_.end());
    wavelengths_.erase(std::unique(wavelengths_.begin(), wavelengths_.end()), wavelengths_.end());
    for (std::size_t kept = 0; kept <= wavelengths_.size(); ++kept) {
        std::vector<NoiseLayer> stack = options_.layers;
        for (NoiseLayer& layer : stack) {
            if (kept < wavelengths_.size() &&
                static_cast<double>(layer.fractal.wavelength) >= wavelengths_[kept]) {
                layer.enabled = false;
            }
        }
        stacks_.push_back(std::move(stack));
    }
}

void NoiseRelief::ApplySamples(SampleBlock& samples, const LayerContext& context) const {
    if (limit_) {
        MapSamples(samples, context.threads, [&](float height, const Vec2d& p) {
            const double resolved = limit_->ValueAt(p.x, p.y);
            const auto kept =
                std::isnan(resolved)
                    ? wavelengths_.size()
                    : static_cast<std::size_t>(
                          std::lower_bound(wavelengths_.begin(), wavelengths_.end(), resolved) -
                          wavelengths_.begin());
            return height + NoiseLayersAt(stacks_[kept], origin_, surface_, p.x, p.y);
        });
        return;
    }
    if (!surface_.IsBody()) {
        ApplyKernel(samples, context, kernel_, origin_);
        return;
    }
    MapSamples(samples, context.threads, [&](float height, const Vec2d& p) {
        return height + NoiseLayersAt(options_.layers, origin_, surface_, p.x, p.y);
    });
}

}  // namespace terraingen
