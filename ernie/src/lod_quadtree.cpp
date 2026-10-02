#include "ernie/lod_quadtree.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ernie {

namespace {

constexpr float kInfinity = std::numeric_limits<float>::infinity();

bool SphereIntersects(const glm::vec3& low, const glm::vec3& high, const glm::vec3& centre,
                      float radius) {
    const glm::vec3 offset = glm::clamp(centre, low, high) - centre;
    return glm::dot(offset, offset) <= radius * radius;
}

}  // namespace

LodQuadtree::LodQuadtree(const RasterView& heights, std::uint32_t leafCells) {
    if (leafCells < 4 || !std::has_single_bit(leafCells)) {
        throw std::invalid_argument("LOD leaf cells must be a power of two of at least 4");
    }
    if (heights.width < 2 || heights.height < 2) {
        throw std::invalid_argument("LOD heightfield must be at least 2 x 2 cells");
    }
    const std::size_t quads = std::max(heights.width, heights.height) - 1;
    std::uint32_t top = 0;
    while ((static_cast<std::size_t>(leafCells) << top) < quads) {
        ++top;
        if (top >= kMaxLevels) {
            throw std::invalid_argument("Heightfield needs more LOD levels than supported");
        }
    }

    leafCells_ = leafCells;
    levels_ = top + 1;
    width_ = static_cast<std::uint32_t>(heights.width);
    height_ = static_cast<std::uint32_t>(heights.height);
    originX_ = static_cast<float>(heights.originX);
    originY_ = static_cast<float>(heights.originY);
    pixelSize_ = static_cast<float>(heights.cellSize);
    heights_.resize(levels_);
    maxHeightSpan_.assign(levels_, 0.0f);

    const glm::vec2 empty(kInfinity, -kInfinity);
    const std::uint32_t leaves = NodesPerSide(0);
    heights_[0].assign(static_cast<std::size_t>(leaves) * leaves, empty);
    for (std::uint32_t y = 0; y < leaves; ++y) {
        const std::uint32_t row0 = y * leafCells;
        if (row0 >= height_ - 1) {
            continue;
        }
        const std::uint32_t row1 = std::min(row0 + leafCells, height_ - 1);
        for (std::uint32_t x = 0; x < leaves; ++x) {
            const std::uint32_t col0 = x * leafCells;
            if (col0 >= width_ - 1) {
                continue;
            }
            const std::uint32_t col1 = std::min(col0 + leafCells, width_ - 1);
            glm::vec2& node = heights_[0][static_cast<std::size_t>(y) * leaves + x];
            for (std::uint32_t row = row0; row <= row1; ++row) {
                for (std::uint32_t col = col0; col <= col1; ++col) {
                    const float h = heights(col, row);
                    if (heights.IsValid(h)) {
                        node.x = std::min(node.x, h);
                        node.y = std::max(node.y, h);
                    }
                }
            }
        }
    }

    for (std::uint32_t level = 1; level < levels_; ++level) {
        const std::uint32_t side = NodesPerSide(level);
        heights_[level].assign(static_cast<std::size_t>(side) * side, empty);
        for (std::uint32_t y = 0; y < side; ++y) {
            for (std::uint32_t x = 0; x < side; ++x) {
                glm::vec2& node = heights_[level][static_cast<std::size_t>(y) * side + x];
                for (std::uint32_t child = 0; child < 4; ++child) {
                    const glm::vec2& h =
                        Heights(level - 1, 2 * x + (child & 1u), 2 * y + (child >> 1));
                    node.x = std::min(node.x, h.x);
                    node.y = std::max(node.y, h.y);
                }
            }
        }
    }

    for (std::uint32_t level = 0; level < levels_; ++level) {
        for (const glm::vec2& node : heights_[level]) {
            if (node.x <= node.y) {
                maxHeightSpan_[level] = std::max(maxHeightSpan_[level], node.y - node.x);
            }
        }
    }
}

float LodQuadtree::MinFirstRange(float exaggeration, float morphStart) const {
    float range = 0.0f;
    for (std::uint32_t level = 0; level + 1 < levels_; ++level) {
        const float size = static_cast<float>(NodeCells(level)) * pixelSize_;
        const float span = maxHeightSpan_[level] * exaggeration;
        const float diagonal = std::sqrt(2.0f * size * size + span * span);
        range = std::max(range, diagonal / (morphStart * static_cast<float>(1u << level)));
    }
    return range;
}

std::vector<LodRange> LodQuadtree::Ranges(float firstRange, float morphStart) const {
    std::vector<LodRange> ranges(levels_);
    float previous = 0.0f;
    for (std::uint32_t level = 0; level < levels_; ++level) {
        if (level + 1 == levels_) {
            ranges[level] = {kInfinity, kInfinity};
            break;
        }
        const float end = firstRange * static_cast<float>(1u << level);
        ranges[level] = {previous + morphStart * (end - previous), end};
        previous = end;
    }
    return ranges;
}

void LodQuadtree::Select(const glm::vec3& eye, std::span<const LodRange> ranges,
                         const Frustum* frustum, float exaggeration,
                         std::vector<LodPatch>& out) const {
    if (!Empty()) {
        SelectNode(levels_ - 1, 0, 0, eye, ranges, frustum, exaggeration, out);
    }
}

void LodQuadtree::SelectAll(std::vector<LodPatch>& out) const {
    if (Empty()) {
        return;
    }
    const std::uint32_t leaves = NodesPerSide(0);
    for (std::uint32_t y = 0; y < leaves; ++y) {
        for (std::uint32_t x = 0; x < leaves; ++x) {
            const glm::vec2& h = Heights(0, x, y);
            if (h.x <= h.y) {
                EmitQuarters(0, x, y, nullptr, 1.0f, out);
            }
        }
    }
}

std::uint64_t LodQuadtree::TriangleCount(std::span<const LodPatch> patches) const {
    const std::uint32_t cells = PatchCells();
    auto quads = [cells](std::uint32_t origin, std::uint32_t last, std::uint32_t spacing) {
        if (origin >= last) {
            return std::uint64_t{0};
        }
        return std::uint64_t{std::min(cells, (last - origin + spacing - 1) / spacing)};
    };
    std::uint64_t triangles = 0;
    for (const LodPatch& patch : patches) {
        const std::uint32_t spacing = 1u << patch.level;
        triangles += 2 * quads(patch.originX, width_ - 1, spacing) *
                     quads(patch.originY, height_ - 1, spacing);
    }
    return triangles;
}

std::pair<glm::vec3, glm::vec3> LodQuadtree::NodeBounds(std::uint32_t level, std::uint32_t x,
                                                        std::uint32_t y, float exaggeration) const {
    const std::uint32_t cells = NodeCells(level);
    const std::uint32_t col0 = std::min(x * cells, width_ - 1);
    const std::uint32_t col1 = std::min(x * cells + cells, width_ - 1);
    const std::uint32_t row0 = std::min(y * cells, height_ - 1);
    const std::uint32_t row1 = std::min(y * cells + cells, height_ - 1);
    const glm::vec2& h = Heights(level, x, y);
    const glm::vec3 low(originX_ + (static_cast<float>(col0) + 0.5f) * pixelSize_,
                        originY_ - (static_cast<float>(row1) + 0.5f) * pixelSize_,
                        h.x * exaggeration);
    const glm::vec3 high(originX_ + (static_cast<float>(col1) + 0.5f) * pixelSize_,
                         originY_ - (static_cast<float>(row0) + 0.5f) * pixelSize_,
                         h.y * exaggeration);
    return {low, high};
}

// Strugar 2009, algorithm 1. Returns false when the node is out of its level's range, so the
// parent covers its area at the parent's level instead.
bool LodQuadtree::SelectNode(std::uint32_t level, std::uint32_t x, std::uint32_t y,
                             const glm::vec3& eye, std::span<const LodRange> ranges,
                             const Frustum* frustum, float exaggeration,
                             std::vector<LodPatch>& out) const {
    const glm::vec2& h = Heights(level, x, y);
    if (h.x > h.y) {
        return true;
    }
    const auto [low, high] = NodeBounds(level, x, y, exaggeration);
    if (level + 1 < levels_ && !SphereIntersects(low, high, eye, ranges[level].morphEnd)) {
        return false;
    }
    if (frustum != nullptr && !frustum->Intersects(low, high)) {
        return true;
    }
    if (level == 0 || !SphereIntersects(low, high, eye, ranges[level - 1].morphEnd)) {
        EmitQuarters(level, x, y, frustum, exaggeration, out);
        return true;
    }
    for (std::uint32_t child = 0; child < 4; ++child) {
        const std::uint32_t childX = 2 * x + (child & 1u);
        const std::uint32_t childY = 2 * y + (child >> 1);
        if (!SelectNode(level - 1, childX, childY, eye, ranges, frustum, exaggeration, out)) {
            EmitChildArea(level, childX, childY, frustum, exaggeration, out);
        }
    }
    return true;
}

void LodQuadtree::EmitQuarters(std::uint32_t level, std::uint32_t x, std::uint32_t y,
                               const Frustum* frustum, float exaggeration,
                               std::vector<LodPatch>& out) const {
    if (level > 0) {
        for (std::uint32_t child = 0; child < 4; ++child) {
            EmitChildArea(level, 2 * x + (child & 1u), 2 * y + (child >> 1), frustum, exaggeration,
                          out);
        }
        return;
    }
    const std::uint32_t cells = PatchCells();
    for (std::uint32_t quarter = 0; quarter < 4; ++quarter) {
        const std::uint32_t originX = x * leafCells_ + (quarter & 1u) * cells;
        const std::uint32_t originY = y * leafCells_ + (quarter >> 1) * cells;
        if (originX < width_ - 1 && originY < height_ - 1) {
            out.push_back({originX, originY, 0, 0});
        }
    }
}

// A quarter of a level-L node covers exactly one level-(L-1) child, so the child's bounds cull it.
void LodQuadtree::EmitChildArea(std::uint32_t level, std::uint32_t childX, std::uint32_t childY,
                                const Frustum* frustum, float exaggeration,
                                std::vector<LodPatch>& out) const {
    const glm::vec2& h = Heights(level - 1, childX, childY);
    if (h.x > h.y) {
        return;
    }
    if (frustum != nullptr) {
        const auto [low, high] = NodeBounds(level - 1, childX, childY, exaggeration);
        if (!frustum->Intersects(low, high)) {
            return;
        }
    }
    const std::uint32_t cells = NodeCells(level - 1);
    out.push_back({childX * cells, childY * cells, level, 0});
}

}  // namespace ernie
