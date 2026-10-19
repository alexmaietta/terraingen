#include "terraingen/pipeline/height_mesh.h"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace terraingen {

namespace {

constexpr std::uint32_t kNoVertex = std::numeric_limits<std::uint32_t>::max();

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

TriangleMesh HeightMesh(const Raster<float>& heights, const Surface& surface, const Vec2d& site) {
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
            if (nw != kNoVertex && ne != kNoVertex && sw != kNoVertex && se != kNoVertex) {
                mesh.triangles.push_back({nw, sw, se});
                mesh.triangles.push_back({nw, se, ne});
            }
        }
    }
    if (mesh.triangles.empty()) {
        throw std::invalid_argument("A height mesh needs a square of four cells with data");
    }
    return mesh;
}

}  // namespace terraingen
