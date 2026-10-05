#pragma once

#include <cstdint>
#include <vector>

namespace ernie {

/// Triangle mesh geometry in a mesh's own frame.
struct MeshData {
    std::vector<float> points;           ///< x, y, z per vertex.
    std::vector<std::uint32_t> indices;  ///< Three vertex indices per triangle.
    std::vector<float> normals;          ///< Empty, or x, y, z per vertex.
    std::vector<float> colors;           ///< Empty, or linear r, g, b per vertex.
};

}  // namespace ernie
