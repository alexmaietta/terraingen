#include "terraingen/pipeline/body_mesh.h"

#include "terraingen/util/cells.h"
#include "terraingen/util/checks.h"

#include <numbers>
#include <stdexcept>
#include <vector>

namespace terraingen {

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
