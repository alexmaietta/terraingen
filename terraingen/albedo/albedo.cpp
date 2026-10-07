#include "terraingen/albedo/albedo.h"

#include "terraingen/util/parallel.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace terraingen {

void ValidateAlbedoContext(const AlbedoContext& context) {
    if (!(std::isfinite(context.spacing) && context.spacing > 0.0)) {
        throw std::invalid_argument("Albedo sample spacing must be positive and finite");
    }
}

void ForEachAlbedoCell(std::vector<Raster<float>>& bands, unsigned threads,
                       const std::function<void(AlbedoSample& value, double x, double y)>& visit) {
    if (bands.empty()) {
        return;
    }
    const Raster<float>& first = bands.front();
    ParallelFor(first.Height(), threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < first.Width(); ++col) {
            AlbedoSample value{};
            for (std::size_t band = 0; band < bands.size(); ++band) {
                value[band] = bands[band](col, row);
            }
            const Vec2d centre = first.CellCenter(col, row);
            visit(value, centre.x, centre.y);
            for (std::size_t band = 0; band < bands.size(); ++band) {
                bands[band](col, row) = value[band];
            }
        }
    });
}

void AlbedoSource::Fill(std::vector<Raster<float>>& bands, const AlbedoContext& context) const {
    ForEachAlbedoCell(bands, context.threads, [&](AlbedoSample& value, double x, double y) {
        value = ValueAt(x, y, context);
    });
}

void AlbedoLayer::Apply(std::vector<Raster<float>>& bands, const AlbedoContext& context) const {
    const std::size_t count = bands.size();
    ForEachAlbedoCell(bands, context.threads, [&](AlbedoSample& value, double x, double y) {
        ApplyAt(value, count, x, y, context);
    });
}

namespace {

std::size_t CheckAlbedo(const Albedo& albedo, const AlbedoContext& context) {
    if (!albedo.source) {
        throw std::invalid_argument("An albedo needs a source");
    }
    for (const auto& layer : albedo.layers) {
        if (!layer) {
            throw std::invalid_argument("An albedo layer is null");
        }
    }
    const std::size_t bands = albedo.source->Bands();
    if (bands != 1 && bands != kMaxAlbedoBands) {
        throw std::invalid_argument("An albedo source must have 1 or 3 bands");
    }
    ValidateAlbedoContext(context);
    return bands;
}

}  // namespace

std::vector<Raster<float>> EvaluateAlbedo(const Albedo& albedo, const RasterGrid& grid,
                                          AlbedoContext context) {
    if (grid.width == 0 || grid.height == 0) {
        throw std::invalid_argument("Cannot evaluate an albedo over an empty grid");
    }
    if (!(std::isfinite(context.spacing) && context.spacing >= 0.0)) {
        throw std::invalid_argument("Albedo sample spacing must be finite and not negative");
    }
    context.spacing = std::fmax(context.spacing, grid.Spacing(albedo.surface));
    const std::size_t count = CheckAlbedo(albedo, context);
    std::vector<Raster<float>> bands;
    for (std::size_t band = 0; band < count; ++band) {
        Raster<float> raster = grid.MakeRaster(0.0f);
        raster.SetNoData(std::numeric_limits<float>::quiet_NaN());
        bands.push_back(std::move(raster));
    }
    albedo.source->Fill(bands, context);
    for (const auto& layer : albedo.layers) {
        layer->Apply(bands, context);
    }
    return bands;
}

AlbedoSample EvaluateAlbedoAt(const Albedo& albedo, double x, double y,
                              const AlbedoContext& context) {
    const std::size_t count = CheckAlbedo(albedo, context);
    AlbedoSample value = albedo.source->ValueAt(x, y, context);
    for (std::size_t band = count; band < kMaxAlbedoBands; ++band) {
        value[band] = 0.0f;
    }
    for (const auto& layer : albedo.layers) {
        layer->ApplyAt(value, count, x, y, context);
    }
    return value;
}

}  // namespace terraingen
