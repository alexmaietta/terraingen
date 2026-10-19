#include "terraingen/pipeline/body_mesh.h"

#include "terraingen/util/cells.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/cube_faces.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/span.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace terraingen {

namespace {

/// Checks a whole-body request: a terrain on a body, at least one cell per face edge and a finite
/// spacing that is not negative.
/// @param what The result, starting the messages, such as "A body mesh".
/// @param cells The cells (or ICQ intervals) per face edge, and how they are called.
void CheckBodyRequest(const Terrain& terrain, std::size_t cells, double spacing,
                      const std::string& what, const std::string& cellsNeeded) {
    if (!terrain.surface.IsBody()) {
        throw std::invalid_argument(what + " needs a terrain on a body");
    }
    if (cells == 0) {
        throw std::invalid_argument(what + " needs " + cellsNeeded);
    }
    RequireNonNegative(spacing, "Sample spacing");
}

/// The body-fixed positions along `directions`: the body's radius in each direction plus the
/// terrain's height there, on the datum where the terrain has no data.
std::vector<Vec3d> BodyPositions(const Terrain& terrain, Span<const Vec3d> directions,
                                 double spacing, unsigned threads) {
    std::vector<Vec2d> points(directions.size());
    for (std::size_t k = 0; k < directions.size(); ++k) {
        points[k] = terrain.surface.Coordinates(directions[k]);
    }
    const std::vector<float> heights = EvaluatePoints(terrain, points, spacing, threads);
    std::vector<Vec3d> positions(directions.size());
    for (std::size_t k = 0; k < directions.size(); ++k) {
        const double r = terrain.surface.RadiusAt(points[k]) +
                         (std::isnan(heights[k]) ? 0.0 : static_cast<double>(heights[k]));
        positions[k] = directions[k] * r;
    }
    return positions;
}

/// Two triangles for each of the `cells` x `cells` quads of each face of a cube grid of
/// `cells + 1` vertices per edge.
std::vector<std::array<std::uint32_t, 3>> CubeTriangles(std::size_t cells) {
    const std::size_t side = cells + 1;
    std::vector<std::array<std::uint32_t, 3>> triangles;
    for (std::size_t face = 0; face < 6; ++face) {
        const auto base = static_cast<std::uint32_t>(face * side * side);
        for (std::size_t j = 0; j < cells; ++j) {
            for (std::size_t i = 0; i < cells; ++i) {
                const auto a = static_cast<std::uint32_t>(base + j * side + i);
                const auto b = a + 1u;
                const auto c = static_cast<std::uint32_t>(a + side);
                const auto d = c + 1u;
                triangles.push_back({a, b, d});
                triangles.push_back({a, d, c});
            }
        }
    }
    return triangles;
}

}  // namespace

TriangleMesh BodyMesh(const Terrain& terrain, std::size_t cellsPerFace, double spacing,
                      unsigned threads) {
    CheckBodyRequest(terrain, cellsPerFace, spacing, "A body mesh", "at least one cell per face");
    const double quarter = detail::kQuarter;
    const double step = 2.0 * quarter / static_cast<double>(cellsPerFace);
    const double sampled = spacing > 0.0 ? spacing : terrain.surface.Radius() * step;
    const std::size_t side = cellsPerFace + 1;
    std::vector<Vec3d> directions(6 * side * side);
    for (std::size_t v = 0; v < directions.size(); ++v) {
        const auto face = static_cast<int>(v / (side * side));
        const std::size_t i = v % side;
        const std::size_t j = (v / side) % side;
        directions[v] = CubeSphereCells::Direction(face, -quarter + static_cast<double>(i) * step,
                                                   -quarter + static_cast<double>(j) * step);
    }
    TriangleMesh mesh;
    mesh.positions = BodyPositions(terrain, directions, sampled, threads);
    mesh.triangles = CubeTriangles(cellsPerFace);
    return mesh;
}

IcqModel BodyIcq(const Terrain& terrain, std::size_t q, double spacing, unsigned threads) {
    CheckBodyRequest(terrain, q, spacing, "An ICQ model", "Q > 0");
    const double sampled =
        spacing > 0.0 ? spacing : terrain.surface.Radius() * kPi / 2.0 / static_cast<double>(q);
    IcqModel model;
    model.q = q;
    std::vector<Vec3d> directions(6 * (q + 1) * (q + 1));
    for (std::size_t face = 1; face <= 6; ++face) {
        for (std::size_t j = 0; j <= q; ++j) {
            for (std::size_t i = 0; i <= q; ++i) {
                directions[model.Index(i, j, face)] = IcqDirection(q, i, j, face);
            }
        }
    }
    model.vertices = BodyPositions(terrain, directions, sampled, threads);
    return model;
}

}  // namespace terraingen
