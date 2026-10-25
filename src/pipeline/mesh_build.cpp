// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/pipeline/mesh_build.h"

#include "util/checks.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace terraingen {

namespace {

static_assert(std::is_standard_layout_v<Vec3d> && sizeof(Vec3d) == 3 * sizeof(double),
              "BuildMesh fills Vec3d positions as packed x, y, z doubles");
static_assert(sizeof(std::array<std::uint32_t, 3>) == 3 * sizeof(std::int32_t),
              "BuildMesh fills triangles as packed index triples");

constexpr std::int32_t kNoVertex = -1;

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

/// The squares of a grid that get two triangles, and the mesh they make.
struct MeshLayout {
    std::size_t width = 0;   ///< Cells per row.
    std::size_t height = 0;  ///< Rows.
    /// 1 for each square, row-major over (width - 1) x (height - 1), whose four cells have data
    /// and that lies outside the cutouts.
    std::vector<unsigned char> squares;
    MeshSize size;  ///< What the mesh holds.

    /// Whether the square from cell (`col`, `row`) is meshed; false outside the grid.
    bool Meshed(std::ptrdiff_t col, std::ptrdiff_t row) const {
        if (col < 0 || row < 0 || static_cast<std::size_t>(col) + 1 >= width ||
            static_cast<std::size_t>(row) + 1 >= height) {
            return false;
        }
        return squares[static_cast<std::size_t>(row) * (width - 1) +
                       static_cast<std::size_t>(col)] != 0;
    }
};

/// Calls `visit(a, b)` with the cell indices of each boundary edge `a` to `b` of the surface
/// triangles, in the order of the triangles and their edges, each edge as its triangle runs.
template <typename Visit> void ForEachBoundaryEdge(const MeshLayout& layout, const Visit& visit) {
    const std::size_t width = layout.width;
    for (std::size_t row = 0; row + 1 < layout.height; ++row) {
        for (std::size_t col = 0; col + 1 < width; ++col) {
            const auto c = static_cast<std::ptrdiff_t>(col);
            const auto r = static_cast<std::ptrdiff_t>(row);
            if (!layout.Meshed(c, r)) {
                continue;
            }
            const std::size_t nw = row * width + col;
            const std::size_t ne = nw + 1;
            const std::size_t sw = nw + width;
            const std::size_t se = sw + 1;
            if (!layout.Meshed(c - 1, r)) {
                visit(nw, sw);
            }
            if (!layout.Meshed(c, r + 1)) {
                visit(sw, se);
            }
            if (!layout.Meshed(c + 1, r)) {
                visit(se, ne);
            }
            if (!layout.Meshed(c, r - 1)) {
                visit(ne, nw);
            }
        }
    }
}

/// The squares `BuildMesh` meshes and the size of its mesh.
/// @throws std::invalid_argument as `GetMeshSize`.
MeshLayout Layout(const Raster<float>& heights, const BuildMeshOptions& options) {
    RequireNonNegative(options.skirt, "Height mesh skirt");
    MeshLayout layout;
    layout.width = heights.Width();
    layout.height = heights.Height();
    for (const float h : heights.Values()) {
        layout.size.vertices += heights.IsNoData(h) ? 0u : 1u;
    }
    std::size_t meshed = 0;
    if (layout.width >= 2 && layout.height >= 2) {
        layout.squares.assign((layout.width - 1) * (layout.height - 1), 0);
        const auto data = [&](std::size_t col, std::size_t row) {
            return !heights.IsNoData(heights(col, row));
        };
        for (std::size_t row = 0; row + 1 < layout.height; ++row) {
            for (std::size_t col = 0; col + 1 < layout.width; ++col) {
                if (data(col, row) && data(col + 1, row) && data(col, row + 1) &&
                    data(col + 1, row + 1) && !CutOut(heights, col, row, options.cutouts)) {
                    layout.squares[row * (layout.width - 1) + col] = 1;
                    ++meshed;
                }
            }
        }
    }
    if (meshed == 0) {
        throw std::invalid_argument("A height mesh needs a square of four cells with data");
    }
    layout.size.triangles = 2 * meshed;
    if (options.skirt > 0.0) {
        std::vector<unsigned char> lowered(heights.Size(), 0);
        ForEachBoundaryEdge(layout, [&](std::size_t a, std::size_t b) {
            layout.size.triangles += 2;
            for (const std::size_t cell : {a, b}) {
                layout.size.vertices += lowered[cell] == 0 ? 1u : 0u;
                lowered[cell] = 1;
            }
        });
    }
    return layout;
}

/// Where a cell center at `height` lies in the frame of `BuildMesh`.
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

/// Throws unless `buffer` holds 3 * `count` values.
template <typename T> void RequireTriples(Span<T> buffer, std::size_t count, const char* what) {
    if (buffer.size() != 3 * count) {
        throw std::invalid_argument(std::string("A height mesh needs ") +
                                    std::to_string(3 * count) + " " + what + ", not " +
                                    std::to_string(buffer.size()));
    }
}

}  // namespace

MeshSize GetMeshSize(const Raster<float>& heights, const BuildMeshOptions& options) {
    return Layout(heights, options).size;
}

void FillMesh(const Raster<float>& heights, Span<double> positions, Span<std::int32_t> triangles,
              const Surface& surface, const Vec2d& site, const BuildMeshOptions& options) {
    const MeshLayout layout = Layout(heights, options);
    RequireTriples(positions, layout.size.vertices, "position coordinates");
    RequireTriples(triangles, layout.size.triangles, "triangle indices");
    if (layout.size.vertices > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::invalid_argument("A height mesh of " + std::to_string(layout.size.vertices) +
                                    " vertices has more than int32 indices can address");
    }
    const SiteFrame frame(surface, site);
    const std::size_t width = layout.width;
    std::vector<std::int32_t> vertex(heights.Size(), kNoVertex);
    std::int32_t next = 0;
    for (std::size_t row = 0; row < layout.height; ++row) {
        for (std::size_t col = 0; col < width; ++col) {
            const float h = heights(col, row);
            if (heights.IsNoData(h)) {
                continue;
            }
            const Vec3d p = frame.Place(heights.CellCenter(col, row), static_cast<double>(h));
            const std::size_t at = 3 * static_cast<std::size_t>(next);
            positions[at] = p.x;
            positions[at + 1] = p.y;
            positions[at + 2] = p.z;
            vertex[row * width + col] = next++;
        }
    }
    std::size_t written = 0;
    const auto emit = [&](std::int32_t a, std::int32_t b, std::int32_t c) {
        triangles[written] = a;
        triangles[written + 1] = b;
        triangles[written + 2] = c;
        written += 3;
    };
    for (std::size_t row = 0; row + 1 < layout.height; ++row) {
        for (std::size_t col = 0; col + 1 < width; ++col) {
            if (!layout.Meshed(static_cast<std::ptrdiff_t>(col),
                               static_cast<std::ptrdiff_t>(row))) {
                continue;
            }
            const std::int32_t nw = vertex[row * width + col];
            const std::int32_t ne = vertex[row * width + col + 1];
            const std::int32_t sw = vertex[(row + 1) * width + col];
            const std::int32_t se = vertex[(row + 1) * width + col + 1];
            emit(nw, sw, se);
            emit(nw, se, ne);
        }
    }
    if (options.skirt > 0.0) {
        std::vector<std::int32_t> lowered(heights.Size(), kNoVertex);
        const auto below = [&](std::size_t cell) {
            if (lowered[cell] == kNoVertex) {
                const auto from = 3 * static_cast<std::size_t>(vertex[cell]);
                const std::size_t to = 3 * static_cast<std::size_t>(next);
                positions[to] = positions[from];
                positions[to + 1] = positions[from + 1];
                positions[to + 2] = positions[from + 2] - options.skirt;
                lowered[cell] = next++;
            }
            return lowered[cell];
        };
        ForEachBoundaryEdge(layout, [&](std::size_t a, std::size_t b) {
            const std::int32_t lowA = below(a);
            const std::int32_t lowB = below(b);
            emit(vertex[b], vertex[a], lowA);
            emit(vertex[b], lowA, lowB);
        });
    }
}

TriangleMesh BuildMesh(const Raster<float>& heights, const Surface& surface, const Vec2d& site,
                       const BuildMeshOptions& options) {
    const MeshSize size = GetMeshSize(heights, options);
    TriangleMesh mesh;
    mesh.positions.resize(size.vertices);
    mesh.triangles.resize(size.triangles);
    FillMesh(heights,
             Span<double>(reinterpret_cast<double*>(mesh.positions.data()), 3 * size.vertices),
             Span<std::int32_t>(reinterpret_cast<std::int32_t*>(mesh.triangles.data()),
                                3 * size.triangles),
             surface, site, options);
    return mesh;
}

}  // namespace terraingen
