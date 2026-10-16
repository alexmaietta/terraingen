#include "terraingen/pipeline/body_mesh.h"

#include "terraingen/util/cells.h"
#include "terraingen/util/checks.h"

#include <numbers>
#include <stdexcept>
#include <vector>

namespace terraingen {

TriangleMesh BodyMesh(const Terrain& terrain, std::size_t cellsPerFace, double spacing,
                      unsigned threads) {
    if (!terrain.surface.IsBody()) {
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
    const std::vector<float> heights = EvaluatePoints(terrain, points, sampled, threads);
    TriangleMesh mesh;
    mesh.positions.resize(directions.size());
    for (std::size_t v = 0; v < directions.size(); ++v) {
        const double r = terrain.surface.RadiusAt(points[v]) +
                         (std::isnan(heights[v]) ? 0.0 : static_cast<double>(heights[v]));
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

IcqModel BodyIcq(const Terrain& terrain, std::size_t q, double spacing, unsigned threads) {
    if (!terrain.surface.IsBody()) {
        throw std::invalid_argument("An ICQ model needs a terrain on a body");
    }
    if (q == 0) {
        throw std::invalid_argument("An ICQ model needs Q > 0");
    }
    RequireNonNegative(spacing, "Sample spacing");
    const double sampled =
        spacing > 0.0 ? spacing
                      : terrain.surface.Radius() * std::numbers::pi / 2.0 / static_cast<double>(q);
    IcqModel model;
    model.q = q;
    model.vertices.resize(6 * (q + 1) * (q + 1));
    std::vector<Vec3d> directions(model.vertices.size());
    std::vector<Vec2d> points(model.vertices.size());
    for (std::size_t face = 1; face <= 6; ++face) {
        for (std::size_t j = 0; j <= q; ++j) {
            for (std::size_t i = 0; i <= q; ++i) {
                const std::size_t k = model.Index(i, j, face);
                directions[k] = IcqDirection(q, i, j, face);
                points[k] = terrain.surface.PointOf(directions[k]);
            }
        }
    }
    const std::vector<float> heights = EvaluatePoints(terrain, points, sampled, threads);
    for (std::size_t k = 0; k < model.vertices.size(); ++k) {
        const double r = terrain.surface.RadiusAt(points[k]) +
                         (std::isnan(heights[k]) ? 0.0 : static_cast<double>(heights[k]));
        model.vertices[k] = directions[k] * r;
    }
    return model;
}

}  // namespace terraingen
