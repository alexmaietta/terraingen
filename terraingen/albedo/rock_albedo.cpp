#include "terraingen/albedo/rock_albedo.h"

#include "terraingen/random/philox.h"
#include "terraingen/random/value_distribution.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

constexpr std::uint32_t kRockStream = 0x524F434Bu;

/// Multiplies every band of `value` by 1 + `share` (`ratio` - 1); NaN stays NaN.
void Brighten(AlbedoSample& value, std::size_t bands, double share, double ratio) {
    if (share == 0.0) {
        return;
    }
    const double factor = 1.0 + share * (ratio - 1.0);
    for (std::size_t band = 0; band < bands; ++band) {
        if (!std::isnan(value[band])) {
            value[band] = static_cast<float>(factor * static_cast<double>(value[band]));
        }
    }
}

}  // namespace

void ValidateRockAlbedo(const RockAlbedoOptions& options) {
    if (!(options.reflectanceRatio > 0.0 && std::isfinite(options.reflectanceRatio))) {
        throw std::invalid_argument("Rock reflectance ratio must be positive and finite");
    }
}

double RockShare(double resolved, const UnresolvedCover& unresolved, double normal) {
    double drawn = 0.0;
    if (unresolved.mean > 0.0) {
        const double s2 = std::log1p(unresolved.variance / (unresolved.mean * unresolved.mean));
        drawn =
            std::fmin(1.0, std::exp(std::log(unresolved.mean) - 0.5 * s2 + std::sqrt(s2) * normal));
    }
    const double kept = std::clamp(resolved, 0.0, 1.0);
    return kept + (1.0 - kept) * drawn;
}

RockAlbedo::RockAlbedo(RockAlbedoOptions options, std::vector<RockSource> sources)
    : options_(options), sources_(std::move(sources)) {
    ValidateRockAlbedo(options_);
    for (const RockSource& source : sources_) {
        if (!source.layer || !source.ground) {
            throw std::invalid_argument(
                "A rock albedo source needs a boulder layer and its ground");
        }
    }
}

double RockAlbedo::Draw(double x, double y, std::size_t source) const {
    const auto xs = std::bit_cast<std::uint64_t>(x);
    const auto ys = std::bit_cast<std::uint64_t>(y);
    const U32x4 bits =
        Philox4x32({static_cast<std::uint32_t>(xs), static_cast<std::uint32_t>(xs >> 32),
                    static_cast<std::uint32_t>(ys), static_cast<std::uint32_t>(ys >> 32)},
                   {options_.seed ^ kRockStream, static_cast<std::uint32_t>(source)});
    return StandardNormal(std::ldexp(static_cast<double>(bits.x), -32),
                          std::ldexp(static_cast<double>(bits.y), -32));
}

double RockAlbedo::ShareAt(double x, double y, const AlbedoContext& context) const {
    ValidateAlbedoContext(context);
    double bare = 1.0;
    for (std::size_t n = 0; n < sources_.size(); ++n) {
        const RockSource& source = sources_[n];
        const double resolved =
            source.layer->MaterialCoverAt(x, y, context.spacing, source.ground.get());
        const UnresolvedCover unresolved =
            options_.unresolved
                ? source.layer->UnresolvedCoverAt(x, y, context.spacing, source.ground.get())
                : UnresolvedCover{};
        bare *= 1.0 - RockShare(resolved, unresolved, options_.unresolved ? Draw(x, y, n) : 0.0);
    }
    return 1.0 - bare;
}

void RockAlbedo::ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                         const AlbedoContext& context) const {
    Brighten(value, bands, ShareAt(x, y, context), options_.reflectanceRatio);
}

void RockAlbedo::Apply(std::vector<Raster<float>>& bands, const AlbedoContext& context) const {
    ValidateAlbedoContext(context);
    if (bands.empty() || sources_.empty()) {
        return;
    }
    const RasterGrid grid = RasterGrid::FromRaster(bands[0]);
    std::vector<double> bare(grid.width * grid.height, 1.0);
    for (std::size_t n = 0; n < sources_.size(); ++n) {
        const RockSource& source = sources_[n];
        const Raster<double> resolved = source.layer->MaterialCoverOver(
            grid, context.spacing, source.ground.get(), context.threads);
        const std::vector<UnresolvedCover> unresolved =
            options_.unresolved ? source.layer->UnresolvedCoverOver(
                                      grid, context.spacing, source.ground.get(), context.threads)
                                : std::vector<UnresolvedCover>(bare.size());
        ParallelFor(grid.height, context.threads, [&](std::size_t row) {
            for (std::size_t col = 0; col < grid.width; ++col) {
                const std::size_t cell = row * grid.width + col;
                const Vec2d centre = bands[0].CellCenter(col, row);
                bare[cell] *=
                    1.0 - RockShare(resolved(col, row), unresolved[cell],
                                    options_.unresolved ? Draw(centre.x, centre.y, n) : 0.0);
            }
        });
    }
    ParallelFor(grid.height, context.threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            AlbedoSample value{};
            for (std::size_t band = 0; band < bands.size(); ++band) {
                value[band] = bands[band](col, row);
            }
            Brighten(value, bands.size(), 1.0 - bare[row * grid.width + col],
                     options_.reflectanceRatio);
            for (std::size_t band = 0; band < bands.size(); ++band) {
                bands[band](col, row) = value[band];
            }
        }
    });
}

}  // namespace terraingen
