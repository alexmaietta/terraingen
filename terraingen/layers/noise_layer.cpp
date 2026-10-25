// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/noise_layer.h"

#include "terraingen/layers/kernel.h"
#include "terraingen/layers/source_resolved.h"
#include "terraingen/noise/surface_noise.h"
#include "terraingen/noise/validate.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// Adds the noise of `fractals` (`FractalStackAt`) to each sample of `samples` holding data, to
/// the same bits: a grid row by row over `threads` threads, or the points in one run
/// (`FractalStackRows`).
void AddNoiseRows(SampleBlock& samples, unsigned threads, const std::vector<Fractal>& fractals,
                  const Vec2d& origin, const Surface& surface) {
    if (Raster<float>* grid = samples.Grid()) {
        ParallelFor(grid->Height(), threads, [&](std::size_t row) {
            std::vector<Vec2d> points(grid->Width());
            std::vector<float> noise(grid->Width());
            for (std::size_t col = 0; col < points.size(); ++col) {
                points[col] = grid->CellCenter(col, row);
            }
            FractalStackRows(fractals, origin, surface, points, noise);
            for (std::size_t col = 0; col < points.size(); ++col) {
                float& cell = (*grid)(col, row);
                if (!grid->IsNoData(cell)) {
                    cell = cell + noise[col];
                }
            }
        });
        return;
    }
    std::vector<Vec2d> points(samples.Size());
    std::vector<float> noise(samples.Size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        points[i] = samples.Position(i);
    }
    FractalStackRows(fractals, origin, surface, points, noise);
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (!samples.IsNoData(i)) {
            samples.Height(i) = samples.Height(i) + noise[i];
        }
    }
}

}  // namespace

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
    Raster<float>* grid = samples.Grid();
    if (!surface_.IsBody() && grid != nullptr && context.backend != Backend::Cpu &&
        RunKernel(context.backend, CompiledKernel<NoiseLayerKernel>(), AsBytes(Span(&kernel_, 1)),
                  *grid, origin_)) {
        return;
    }
    AddNoiseRows(samples, context.threads, options_.fractals, origin_, surface_);
}

}  // namespace terraingen
