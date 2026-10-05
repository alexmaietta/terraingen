#include "ernie/geometry/mesh_data.h"

#include <stdexcept>

namespace ernie {

std::vector<std::uint32_t> TriangulateFaces(std::span<const std::int64_t> faceVertexCounts,
                                            std::span<const std::int64_t> faceVertexIndices,
                                            std::size_t pointCount) {
    std::vector<std::uint32_t> triangles;
    std::size_t next = 0;
    for (const std::int64_t count : faceVertexCounts) {
        if (count < 3) {
            throw std::invalid_argument("Every face needs at least 3 vertices");
        }
        if (next + static_cast<std::size_t>(count) > faceVertexIndices.size()) {
            throw std::invalid_argument("faceVertexCounts add up to more than faceVertexIndices");
        }
        for (std::size_t k = 0; k < static_cast<std::size_t>(count); ++k) {
            const std::int64_t index = faceVertexIndices[next + k];
            if (index < 0 || static_cast<std::size_t>(index) >= pointCount) {
                throw std::invalid_argument("faceVertexIndices has an index out of range");
            }
        }
        for (std::size_t k = 1; k + 1 < static_cast<std::size_t>(count); ++k) {
            triangles.push_back(static_cast<std::uint32_t>(faceVertexIndices[next]));
            triangles.push_back(static_cast<std::uint32_t>(faceVertexIndices[next + k]));
            triangles.push_back(static_cast<std::uint32_t>(faceVertexIndices[next + k + 1]));
        }
        next += static_cast<std::size_t>(count);
    }
    if (next != faceVertexIndices.size()) {
        throw std::invalid_argument("faceVertexCounts add up to fewer than faceVertexIndices");
    }
    return triangles;
}

}  // namespace ernie
