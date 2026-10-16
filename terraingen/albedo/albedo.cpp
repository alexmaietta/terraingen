#include "terraingen/albedo/albedo.h"

#include "terraingen/util/checks.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace terraingen {

void ValidateAlbedoContext(const AlbedoContext& context) {
    RequirePositive(context.spacing, "Albedo sample spacing");
}

namespace {

/// Visits the cells of one row of `bands`.
void VisitRow(std::vector<Raster<float>>& bands, std::size_t row, const AlbedoCellVisitor& visit) {
    const Raster<float>& first = bands.front();
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
}

}  // namespace

void ForEachAlbedoCell(std::vector<Raster<float>>& bands, unsigned threads,
                       const AlbedoCellVisitor& visit) {
    if (bands.empty()) {
        return;
    }
    ParallelFor(bands.front().Height(), threads,
                [&](std::size_t row) { VisitRow(bands, row, visit); });
}

void ForEachAlbedoBlock(
    std::vector<Raster<float>>& bands, unsigned threads, std::size_t blockRows,
    const std::function<AlbedoCellVisitor(std::size_t rowBegin, std::size_t rowEnd)>& visitorFor) {
    if (bands.empty()) {
        return;
    }
    const std::size_t height = bands.front().Height();
    const std::size_t blocks = (height + blockRows - 1) / blockRows;
    ParallelFor(blocks, threads, [&](std::size_t block) {
        const std::size_t rowBegin = block * blockRows;
        const std::size_t rowEnd = std::min(height, rowBegin + blockRows);
        const AlbedoCellVisitor visit = visitorFor(rowBegin, rowEnd);
        for (std::size_t row = rowBegin; row < rowEnd; ++row) {
            VisitRow(bands, row, visit);
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
    RequireNonNegative(context.spacing, "Albedo sample spacing");
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
