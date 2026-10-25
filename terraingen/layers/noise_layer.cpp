// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/noise_layer.h"

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

NoiseLayer::NoiseLayer(NoiseLayerOptions options, TerrainBase base)
    : options_(std::move(options)), origin_(base.ReferencePoint()), surface_(base.surface) {
    ValidateFractals(options_.fractals, 1);
    ValidateSourceResolvesPixels(options_.sourceResolvesPixels);
    kernel_ = MakeNoiseLayerKernel(options_.fractals);
    limit_ = LayerResolvedLimit(base.source, options_.sourceResolvesPixels, surface_);
    if (!limit_) {
        return;
    }
    for (const Fractal& fractal : options_.fractals) {
        wavelengths_.push_back(static_cast<double>(fractal.wavelength));
    }
    std::sort(wavelengths_.begin(), wavelengths_.end());
    wavelengths_.erase(std::unique(wavelengths_.begin(), wavelengths_.end()), wavelengths_.end());
    for (std::size_t kept = 0; kept <= wavelengths_.size(); ++kept) {
        std::vector<Fractal> stack = options_.fractals;
        for (Fractal& fractal : stack) {
            if (kept < wavelengths_.size() &&
                static_cast<double>(fractal.wavelength) >= wavelengths_[kept]) {
                fractal.enabled = false;
            }
        }
        stacks_.push_back(std::move(stack));
    }
}

void NoiseLayer::ApplySamples(SampleBlock& samples, const LayerContext& context) const {
    if (limit_) {
        MapSamples(samples, context.threads, [&](float height, const Vec2d& p) {
            const double resolved = limit_->ValueAt(p.x, p.y);
            const auto kept =
                std::isnan(resolved)
                    ? wavelengths_.size()
                    : static_cast<std::size_t>(
                          std::lower_bound(wavelengths_.begin(), wavelengths_.end(), resolved) -
                          wavelengths_.begin());
            return height + FractalStackAt(stacks_[kept], origin_, surface_, p.x, p.y);
        });
        return;
    }
    if (!surface_.IsBody()) {
        ApplyKernel(samples, context, kernel_, origin_);
        return;
    }
    MapSamples(samples, context.threads, [&](float height, const Vec2d& p) {
        return height + FractalStackAt(options_.fractals, origin_, surface_, p.x, p.y);
    });
}

}  // namespace terraingen
