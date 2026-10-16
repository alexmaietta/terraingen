/// @file
/// @brief Evaluating a terrain over grids, tiles, points and a whole body.
#include "terraingen/pipeline/terrain.h"

#include "terraingen/util/cells.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace terraingen {

namespace {

void CheckTerrain(const Terrain& terrain) {
    if (!terrain.source) {
        throw std::invalid_argument("A terrain needs a source");
    }
    if (std::any_of(terrain.layers.begin(), terrain.layers.end(),
                    [](const auto& l) { return !l; }) ||
        std::any_of(terrain.filters.begin(), terrain.filters.end(),
                    [](const auto& f) { return !f; })) {
        throw std::invalid_argument("A terrain's layers and filters must not be null");
    }
}

/// Sets `heights` to the terrain's source at `points`, then applies its first `layers` layers to
/// them all at once, each on the ground beneath it.
void EvaluateStack(const Terrain& terrain, std::size_t layers, std::span<const Vec2d> points,
                   std::span<float> heights, double spacing) {
    for (std::size_t i = 0; i < points.size(); ++i) {
        heights[i] = terrain.source->HeightAt(points[i].x, points[i].y);
    }
    TerrainFeatures features;
    for (std::size_t n = 0; n < layers; ++n) {
        const TerrainGround ground(terrain, n);
        SampleBlock samples(points, heights);
        terrain.layers[n]->Apply(samples, LayerContext{spacing, 1, &ground}, features);
    }
}

}  // namespace

EvaluatedTerrain Evaluate(const Terrain& terrain, const RasterGrid& grid, double spacing) {
    return Evaluate(terrain, grid, EvaluationOptions{spacing, 1});
}

EvaluatedTerrain Evaluate(const Terrain& terrain, const RasterGrid& grid,
                          const EvaluationOptions& options) {
    CheckTerrain(terrain);
    RequireNonNegative(options.spacing, "Sample spacing");
    const double resolved = std::max(options.spacing, grid.Spacing(terrain.surface));
    EvaluatedTerrain result{grid.MakeRaster(), {}};
    terrain.source->Fill(result.heights);
    for (std::size_t n = 0; n < terrain.layers.size(); ++n) {
        const TerrainGround ground(terrain, n);
        terrain.layers[n]->Apply(result.heights, LayerContext{resolved, options.threads, &ground},
                                 result.features);
    }
    for (const auto& filter : terrain.filters) {
        filter->Apply(result.heights, {resolved, options.threads}, result.features);
    }
    return result;
}

double FilterMargin(const Terrain& terrain, double spacing) {
    CheckTerrain(terrain);
    double largest = 0.0;
    for (const auto& layer : terrain.layers) {
        largest = std::fmax(largest, layer->MaxFeatureDiameter());
    }
    double margin = 0.0;
    for (const auto& filter : terrain.filters) {
        margin = std::fmax(margin, filter->Margin(largest, spacing));
    }
    return margin;
}

EvaluatedTerrain EvaluateTile(const Terrain& terrain, const TileScheme& scheme, const TileKey& key,
                              unsigned threads) {
    scheme.Validate();
    const RasterGrid tile = scheme.Grid(key);
    const double spacing = tile.Spacing(terrain.surface);
    const double metres = FilterMargin(terrain, spacing);
    std::size_t marginRows = static_cast<std::size_t>(std::ceil(metres / spacing));
    std::size_t marginColumns = marginRows;
    if (terrain.surface.IsSphere() && metres > 0.0) {
        const Bounds2d bounds = tile.Bounds();
        const Bounds2d grown = terrain.surface.Expand(bounds, metres);
        const double cell = tile.transform.pixelWidth;
        marginColumns = static_cast<std::size_t>(
            std::ceil(std::fmax(bounds.minX - grown.minX, grown.maxX - bounds.maxX) / cell));
        marginColumns = std::min(marginColumns, static_cast<std::size_t>(std::ceil(360.0 / cell)));
    }
    if (marginRows == 0) {
        return Evaluate(terrain, tile, EvaluationOptions{spacing, threads});
    }
    const std::size_t margin = marginRows;
    const Vec2d corner = tile.transform.PixelToWorld(-static_cast<double>(marginColumns),
                                                     -static_cast<double>(marginRows));
    GeoTransform paddedTransform = tile.transform;
    paddedTransform.originX = corner.x;
    paddedTransform.originY = corner.y;
    const RasterGrid padded{tile.width + 2 * marginColumns, tile.height + 2 * marginRows,
                            paddedTransform};
    EvaluatedTerrain evaluated = Evaluate(terrain, padded, EvaluationOptions{spacing, threads});
    EvaluatedTerrain result{tile.MakeRaster(), std::move(evaluated.features)};
    result.heights.SetNoData(evaluated.heights.NoData());
    for (std::size_t row = 0; row < tile.height; ++row) {
        for (std::size_t col = 0; col < tile.width; ++col) {
            result.heights(col, row) = evaluated.heights(col + marginColumns, row + margin);
        }
    }
    return result;
}

std::vector<EvaluatedTerrain> EvaluateTiles(const Terrain& terrain, const TileScheme& scheme,
                                            const std::vector<TileKey>& keys, unsigned threads) {
    std::vector<EvaluatedTerrain> tiles(keys.size());
    ParallelFor(keys.size(), threads,
                [&](std::size_t n) { tiles[n] = EvaluateTile(terrain, scheme, keys[n], 1); });
    return tiles;
}

std::vector<float> EvaluatePoints(const Terrain& terrain, std::span<const Vec2d> points,
                                  double spacing, unsigned threads) {
    CheckTerrain(terrain);
    if (!terrain.filters.empty()) {
        throw std::invalid_argument(
            "Point queries cannot run filters, which read neighbouring cells; evaluate a grid");
    }
    RequirePositive(spacing, "Sample spacing");
    constexpr std::size_t kBatch = 256;
    std::vector<float> heights(points.size());
    ParallelFor((points.size() + kBatch - 1) / kBatch, threads, [&](std::size_t batch) {
        const std::size_t begin = batch * kBatch;
        const std::size_t count = std::min(kBatch, points.size() - begin);
        EvaluateStack(terrain, terrain.layers.size(), points.subspan(begin, count),
                      std::span<float>(heights).subspan(begin, count), spacing);
    });
    return heights;
}

float EvaluateAt(const Terrain& terrain, double x, double y, double spacing) {
    const Vec2d point{x, y};
    return EvaluatePoints(terrain, {&point, 1}, spacing, 1)[0];
}

TerrainGround::TerrainGround(const Terrain& terrain, std::size_t layers)
    : terrain_(terrain), layers_(layers) {
    if (!terrain_.source) {
        throw std::invalid_argument("A terrain needs a source");
    }
    if (layers_ > terrain_.layers.size()) {
        throw std::invalid_argument("The terrain has fewer layers than the ground asks for");
    }
}

float TerrainGround::HeightAt(double x, double y, double spacing) const {
    const Vec2d point{x, y};
    float height = 0.0f;
    EvaluateStack(terrain_, layers_, {&point, 1}, {&height, 1}, spacing);
    return height;
}

EvaluatedTerrain Evaluate(const Terrain& terrain) {
    CheckTerrain(terrain);
    return Evaluate(terrain, terrain.source->Grid());
}

TriangleMesh BodyMesh(const Terrain& terrain, std::size_t cellsPerFace, double spacing,
                      unsigned threads) {
    if (!terrain.surface.IsSphere()) {
        throw std::invalid_argument("A body mesh needs a terrain on a sphere");
    }
    if (cellsPerFace == 0) {
        throw std::invalid_argument("A body mesh needs at least one cell per face");
    }
    RequireNonNegative(spacing, "Sample spacing");
    const double radius = terrain.surface.Radius();
    const double quarter = std::numbers::pi / 4.0;
    const double step = 2.0 * quarter / static_cast<double>(cellsPerFace);
    const double sampled = spacing > 0.0 ? spacing : radius * step;
    Terrain unfiltered = terrain;
    unfiltered.filters.clear();
    const std::size_t side = cellsPerFace + 1;
    std::vector<Vec3d> directions(6 * side * side);
    std::vector<Vec2d> points(directions.size());
    for (std::size_t v = 0; v < directions.size(); ++v) {
        const auto face = static_cast<int>(v / (side * side));
        const std::size_t i = v % side;
        const std::size_t j = (v / side) % side;
        directions[v] = CubeSphereCells::Direction(face, -quarter + static_cast<double>(i) * step,
                                                   -quarter + static_cast<double>(j) * step);
        points[v] = terrain.surface.PointOf(directions[v]);
    }
    const std::vector<float> heights = EvaluatePoints(unfiltered, points, sampled, threads);
    TriangleMesh mesh;
    mesh.positions.resize(directions.size());
    for (std::size_t v = 0; v < directions.size(); ++v) {
        const double r = radius + (std::isnan(heights[v]) ? 0.0 : static_cast<double>(heights[v]));
        mesh.positions[v] = directions[v] * r;
    }
    for (std::size_t face = 0; face < 6; ++face) {
        const auto base = static_cast<std::uint32_t>(face * side * side);
        for (std::size_t j = 0; j < cellsPerFace; ++j) {
            for (std::size_t i = 0; i < cellsPerFace; ++i) {
                const auto a = static_cast<std::uint32_t>(base + j * side + i);
                const auto b = a + 1u;
                const auto c = static_cast<std::uint32_t>(a + side);
                const auto d = c + 1u;
                mesh.triangles.push_back({a, b, d});
                mesh.triangles.push_back({a, d, c});
            }
        }
    }
    return mesh;
}

}  // namespace terraingen
