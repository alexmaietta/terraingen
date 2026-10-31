// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/pipeline/body_mesh.h"

#include "terraingen/geometry/geometry.h"
#include "terraingen/util/span.h"
#include "util/checks.h"

#include <cmath>
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
    if (!terrain.Surface().IsBody()) {
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
    const Surface& surface = terrain.Surface();
    std::vector<Vec2d> points(directions.size());
    for (std::size_t k = 0; k < directions.size(); ++k) {
        points[k] = surface.Coordinates(directions[k]);
    }
    std::vector<float> heights(points.size());
    terrain.HeightsAt(points, heights, spacing, threads);
    std::vector<Vec3d> positions(directions.size());
    for (std::size_t k = 0; k < directions.size(); ++k) {
        const double r = surface.RadiusAt(points[k]) +
                         (std::isnan(heights[k]) ? 0.0 : static_cast<double>(heights[k]));
        positions[k] = directions[k] * r;
    }
    return positions;
}

}  // namespace

TriangleMesh BodyMesh(const Terrain& terrain, std::size_t cellsPerFace, double spacing,
                      unsigned threads) {
    CheckBodyRequest(terrain, cellsPerFace, spacing, "A body mesh", "at least one cell per face");
    return IcqToMesh(BodyIcq(terrain, cellsPerFace, spacing, threads));
}

IcqModel BodyIcq(const Terrain& terrain, std::size_t q, double spacing, unsigned threads) {
    CheckBodyRequest(terrain, q, spacing, "An ICQ model", "Q > 0");
    const double sampled =
        spacing > 0.0 ? spacing : terrain.Surface().Radius() * kPi / 2.0 / static_cast<double>(q);
    IcqModel model;
    model.q = q;
    std::vector<Vec3d> directions(6 * (q + 1) * (q + 1));
    ForEachIcqLabel(q, [&](std::size_t i, std::size_t j, std::size_t face, std::size_t k) {
        directions[k] = IcqDirection(q, i, j, face);
    });
    model.vertices = BodyPositions(terrain, directions, sampled, threads);
    return model;
}

}  // namespace terraingen
