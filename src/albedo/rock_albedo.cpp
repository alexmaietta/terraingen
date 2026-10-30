// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/albedo/rock_albedo.h"

#include "albedo/bands.h"
#include "terraingen/albedo/albedo_fields.h"
#include "terraingen/noise/hash.h"
#include "terraingen/random/samplers.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

void ValidateRockAlbedoOptions(const RockAlbedoOptions& options) {
    CheckRanges(options);
}

double RockShare(double resolved, const UnresolvedCover& unresolved, double normal) {
    double drawn = 0.0;
    if (unresolved.mean > 0.0) {
        const double s2 = std::log1p(unresolved.variance / (unresolved.mean * unresolved.mean));
        drawn =
            std::fmin(1.0, std::exp(std::log(unresolved.mean) - 0.5 * s2 + std::sqrt(s2) * normal));
    }
    const double clamped = std::clamp(resolved, 0.0, 1.0);
    return clamped + (1.0 - clamped) * drawn;
}

RockAlbedo::RockAlbedo(RockAlbedoOptions options, std::vector<RockSource> sources)
    : options_(options), sources_(std::move(sources)) {
    ValidateRockAlbedoOptions(options_);
    for (const RockSource& source : sources_) {
        if (!source.layer || !source.ground) {
            throw std::invalid_argument(
                "A rock albedo source needs a boulder layer and its ground");
        }
    }
}

namespace {

/// The context `source`'s boulder layer is read in for an albedo evaluated in `context`.
LayerContext LayerOn(const RockSource& source, const AlbedoContext& context) {
    LayerContext layer;
    layer.spacing = context.spacing;
    layer.threads = context.threads;
    layer.ground = source.ground.get();
    layer.surface = context.surface;
    layer.source = context.source;
    return layer;
}

}  // namespace

double RockAlbedo::UnresolvedNormal(double x, double y, std::size_t source) const {
    RandomStream random(options_.seed, kRockAlbedoStream, HashWord64(BitCast<std::uint64_t>(x)),
                        HashWord64(BitCast<std::uint64_t>(y)), static_cast<std::uint32_t>(source));
    return StandardNormal(random);
}

double RockAlbedo::ShareAt(double x, double y, const AlbedoContext& context) const {
    ValidateAlbedoContext(context);
    double bare = 1.0;
    for (std::size_t n = 0; n < sources_.size(); ++n) {
        const RockSource& source = sources_[n];
        const double resolved = source.layer->MaterialCoverAt(x, y, LayerOn(source, context));
        const UnresolvedCover unresolved =
            options_.unresolved
                ? source.layer->UnresolvedCoverAt(x, y, context.spacing, source.ground.get())
                : UnresolvedCover{};
        bare *= 1.0 - RockShare(resolved, unresolved,
                                options_.unresolved ? UnresolvedNormal(x, y, n) : 0.0);
    }
    return 1.0 - bare;
}

void RockAlbedo::ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                         const AlbedoContext& context) const {
    ChangeBands(value, bands, ShareAt(x, y, context) * (options_.reflectanceRatio - 1.0));
}

std::vector<double> RockAlbedo::BareShares(const Raster<float>& first,
                                           const AlbedoContext& context) const {
    const RasterGrid grid = RasterGrid::FromRaster(first);
    std::vector<double> bare(grid.width * grid.height, 1.0);
    for (std::size_t n = 0; n < sources_.size(); ++n) {
        const RockSource& source = sources_[n];
        const Raster<double> resolved =
            source.layer->MaterialCoverOver(grid, LayerOn(source, context));
        const std::vector<UnresolvedCover> unresolved =
            options_.unresolved ? source.layer->UnresolvedCoverOver(
                                      grid, context.spacing, source.ground.get(), context.threads)
                                : std::vector<UnresolvedCover>(bare.size());
        ParallelFor(grid.height, context.threads, [&](std::size_t row) {
            for (std::size_t col = 0; col < grid.width; ++col) {
                const std::size_t cell = row * grid.width + col;
                const Vec2d center = first.CellCenter(col, row);
                bare[cell] *=
                    1.0 -
                    RockShare(resolved(col, row), unresolved[cell],
                              options_.unresolved ? UnresolvedNormal(center.x, center.y, n) : 0.0);
            }
        });
    }
    return bare;
}

void RockAlbedo::Apply(std::vector<Raster<float>>& bands, const AlbedoContext& context) const {
    if (bands.empty() || sources_.empty()) {
        return;
    }
    const std::vector<double> bare = BareShares(bands[0], context);
    const std::size_t width = bands[0].Width();
    const std::size_t count = bands.size();
    ForEachAlbedoPixel(
        bands, context.threads, [&](AlbedoSample& value, std::size_t col, std::size_t row) {
            ChangeBands(value, count,
                        (1.0 - bare[row * width + col]) * (options_.reflectanceRatio - 1.0));
        });
}

}  // namespace terraingen
