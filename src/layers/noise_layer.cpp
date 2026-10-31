// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/noise_layer.h"

#include "layers/kernel.h"
#include "terraingen/layers/source_resolved.h"
#include "terraingen/noise/surface_noise.h"
#include "terraingen/noise/validate.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
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

NoiseLayer::NoiseLayer(NoiseLayerOptions options) : m_options(std::move(options)) {
    ValidateFractals(m_options.fractals, 1);
    ValidateSourceResolvesPixels(m_options.sourceResolvesPixels);
    m_kernel = MakeNoiseLayerKernel(m_options.fractals);
    if (!m_options.sourceResolvesPixels) {
        return;
    }
    for (const Fractal& fractal : m_options.fractals) {
        m_wavelengths.push_back(static_cast<double>(fractal.wavelength));
    }
    std::sort(m_wavelengths.begin(), m_wavelengths.end());
    m_wavelengths.erase(std::unique(m_wavelengths.begin(), m_wavelengths.end()),
                        m_wavelengths.end());
    for (std::size_t kept = 0; kept <= m_wavelengths.size(); ++kept) {
        std::vector<Fractal> stack = m_options.fractals;
        for (Fractal& fractal : stack) {
            if (kept < m_wavelengths.size() &&
                static_cast<double>(fractal.wavelength) >= m_wavelengths[kept]) {
                fractal.enabled = false;
            }
        }
        m_stacks.push_back(std::move(stack));
    }
}

LayerPtr NoiseLayer::Reseeded(std::uint64_t seed) const {
    NoiseLayerOptions options = m_options;
    for (std::size_t n = 0; n < options.fractals.size(); ++n) {
        options.fractals[n].seed = ReseedWord(seed, static_cast<std::uint32_t>(n));
    }
    return std::make_shared<NoiseLayer>(std::move(options));
}

void NoiseLayer::ApplySamples(SampleBlock& samples, const LayerContext& context) const {
    const Surface& surface = context.SurfaceOrPlane();
    const Vec2d origin = context.ReferencePoint();
    if (context.source != nullptr && m_options.sourceResolvesPixels) {
        MapSamples(samples, context.threads, [&](float height, const Vec2d& p) {
            const double resolved = SourceResolvedSize(
                context.source, m_options.sourceResolvesPixels, surface, p.x, p.y);
            const auto kept =
                std::isnan(resolved)
                    ? m_wavelengths.size()
                    : static_cast<std::size_t>(
                          std::lower_bound(m_wavelengths.begin(), m_wavelengths.end(), resolved) -
                          m_wavelengths.begin());
            return height + FractalStackAt(m_stacks[kept], origin, surface, p.x, p.y);
        });
        return;
    }
    Raster<float>* grid = samples.Grid();
    if (!surface.IsBody() && grid != nullptr && context.backend != Backend::Cpu &&
        RunKernel(context.backend, CompiledKernel<NoiseLayerKernel>(), AsBytes(Span(&m_kernel, 1)),
                  *grid, origin)) {
        return;
    }
    AddNoiseRows(samples, context.threads, m_options.fractals, origin, surface);
}

}  // namespace terraingen
