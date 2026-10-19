#include "terraingen/pipeline/height_mesh.h"

#include "terraingen/util/checks.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

constexpr std::uint32_t kNoVertex = std::numeric_limits<std::uint32_t>::max();

/// Whether the square of cells from (`col`, `row`) to the next column and row lies in a cutout.
bool CutOut(const Raster<float>& heights, std::size_t col, std::size_t row,
            const std::vector<Bounds2d>& cutouts) {
    const Vec2d a = heights.CellCenter(col, row);
    const Vec2d b = heights.CellCenter(col + 1, row + 1);
    return std::any_of(cutouts.begin(), cutouts.end(), [&](const Bounds2d& box) {
        const auto inside = [&](const Vec2d& p) {
            return p.x >= box.minX && p.x <= box.maxX && p.y >= box.minY && p.y <= box.maxY;
        };
        return inside(a) && inside(b) && inside({a.x, b.y}) && inside({b.x, a.y});
    });
}

/// Hangs a wall `depth` below every edge of `mesh` that only one triangle has, facing out.
void AddSkirt(TriangleMesh& mesh, double depth) {
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> edges;
    for (const auto& triangle : mesh.triangles) {
        for (std::size_t k = 0; k < 3; ++k) {
            ++edges[{triangle[k], triangle[(k + 1) % 3]}];
        }
    }
    std::map<std::uint32_t, std::uint32_t> lowered;
    const auto below = [&](std::uint32_t vertex) {
        const auto found = lowered.find(vertex);
        if (found != lowered.end()) {
            return found->second;
        }
        const auto index = static_cast<std::uint32_t>(mesh.positions.size());
        Vec3d position = mesh.positions[vertex];
        position.z -= depth;
        mesh.positions.push_back(position);
        lowered.emplace(vertex, index);
        return index;
    };
    const std::size_t surfaceTriangles = mesh.triangles.size();
    for (std::size_t t = 0; t < surfaceTriangles; ++t) {
        const auto triangle = mesh.triangles[t];
        for (std::size_t k = 0; k < 3; ++k) {
            const std::uint32_t a = triangle[k];
            const std::uint32_t b = triangle[(k + 1) % 3];
            if (edges.count({b, a}) != 0) {
                continue;
            }
            const std::uint32_t lowA = below(a);
            const std::uint32_t lowB = below(b);
            mesh.triangles.push_back({b, a, lowA});
            mesh.triangles.push_back({b, lowA, lowB});
        }
    }
}

/// Where a cell centre at `height` lies in the frame of `HeightMesh`.
class SiteFrame {
  public:
    SiteFrame(const Surface& surface, const Vec2d& site)
        : surface_(surface), frame_(surface.Frame(site)), origin_(surface.Position(site)) {}

    Vec3d Place(const Vec2d& point, double height) const {
        if (!surface_.IsBody()) {
            return {point.x, point.y, height};
        }
        const LocalFrame at = surface_.Frame(point);
        const Vec3d p = surface_.Position(point) + at.up * height;
        const Vec3d d = p - origin_;
        return {Dot(frame_.east, d), Dot(frame_.north, d), Dot(frame_.up, d)};
    }

  private:
    const Surface& surface_;
    LocalFrame frame_;
    Vec3d origin_;
};

}  // namespace

TriangleMesh HeightMesh(const Raster<float>& heights, const Surface& surface, const Vec2d& site,
                        const HeightMeshOptions& options) {
    RequireNonNegative(options.skirt, "Height mesh skirt");
    const SiteFrame frame(surface, site);
    const std::size_t width = heights.Width();
    TriangleMesh mesh;
    std::vector<std::uint32_t> vertex(heights.Size(), kNoVertex);
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < width; ++col) {
            const float h = heights(col, row);
            if (!heights.IsNoData(h)) {
                vertex[row * width + col] = static_cast<std::uint32_t>(mesh.positions.size());
                mesh.positions.push_back(
                    frame.Place(heights.CellCenter(col, row), static_cast<double>(h)));
            }
        }
    }
    for (std::size_t row = 0; row + 1 < heights.Height(); ++row) {
        for (std::size_t col = 0; col + 1 < width; ++col) {
            const std::uint32_t nw = vertex[row * width + col];
            const std::uint32_t ne = vertex[row * width + col + 1];
            const std::uint32_t sw = vertex[(row + 1) * width + col];
            const std::uint32_t se = vertex[(row + 1) * width + col + 1];
            if (nw != kNoVertex && ne != kNoVertex && sw != kNoVertex && se != kNoVertex &&
                !CutOut(heights, col, row, options.cutouts)) {
                mesh.triangles.push_back({nw, sw, se});
                mesh.triangles.push_back({nw, se, ne});
            }
        }
    }
    if (mesh.triangles.empty()) {
        throw std::invalid_argument("A height mesh needs a square of four cells with data");
    }
    if (options.skirt > 0.0) {
        AddSkirt(mesh, options.skirt);
    }
    return mesh;
}

}  // namespace terraingen
