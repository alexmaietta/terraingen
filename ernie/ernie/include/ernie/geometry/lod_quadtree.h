#pragma once

#include "ernie/core/raster_view.h"
#include "ernie/geometry/frustum.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace ernie {

/// One square patch of `PatchCells()` x `PatchCells()` grid quads whose vertices are `2^level`
/// raster cells apart. Matches the WGSL `vec4<u32>` the terrain shaders read per instance.
struct LodPatch {
    std::uint32_t originX = 0;  ///< Raster column of the patch's north-west vertex.
    std::uint32_t originY = 0;  ///< Raster row of the patch's north-west vertex.
    std::uint32_t level = 0;
    std::uint32_t pad = 0;

    bool operator==(const LodPatch&) const = default;
};

/// Distances from the LOD eye, in world units, over which one level's vertices morph towards the
/// next coarser level's. `morphEnd` is also the level's selection range.
struct LodRange {
    float morphStart = 0.0f;
    float morphEnd = 0.0f;
};

/// Quadtree over a heightfield for continuous distance-dependent level of detail (CDLOD).
///
/// A level-0 node spans `leafCells` x `leafCells` grid quads; a level-L node spans `leafCells *
/// 2^L` and the single root node covers the whole raster. The tree stores only each node's min and
/// max height, so selection can bound nodes in 3D for range and frustum tests.
///
/// `Select` walks the tree from the root and picks, for each region, the coarsest level whose
/// range covers it (Strugar 2009, algorithm 1). Every node is drawn as its four quarters, so all
/// patches share one `PatchCells()` grid and differ only in origin and vertex spacing.
///
/// Within each level's range the vertex shader morphs vertices towards the next coarser level, so
/// switching level never pops; see `terrain.slang`. With a first range of at least
/// `MinFirstRange`, neighbouring patches differ by at most one level and their shared edges match
/// exactly, so no skirts or stitching are needed.
///
/// References:
/// - F. Strugar, "Continuous Distance-Dependent Level of Detail for Rendering Heightmaps", Journal
///   of Graphics, GPU, and Game Tools 14(4):57-74, 2009.
///   https://doi.org/10.1080/2151237X.2009.10129287
/// - P. Lindstrom, D. Koller, W. Ribarsky, L. F. Hodges, N. Faust and G. A. Turner, "Real-Time,
///   Continuous Level of Detail Rendering of Height Fields", SIGGRAPH '96, 109-118, 1996.
///   https://doi.org/10.1145/237170.237217
class LodQuadtree {
  public:
    /// Most levels a tree may have; the shaders size their per-level morph table to match.
    static constexpr std::uint32_t kMaxLevels = 16;

    LodQuadtree() = default;

    /// Builds the min/max height pyramid of `heights`. Cells that are not finite or equal the
    /// nodata value are ignored, and nodes without valid cells are never selected. The tree keeps
    /// no reference to `heights`.
    /// @param leafCells Grid quads along a level-0 node; a power of two of at least 4.
    /// @throws std::invalid_argument if `leafCells` is invalid, the raster is smaller than 2 x 2,
    /// or the tree would need more than `kMaxLevels` levels.
    LodQuadtree(const RasterView& heights, std::uint32_t leafCells);

    bool Empty() const {
        return levels_ == 0;
    }

    std::uint32_t LeafCells() const {
        return leafCells_;
    }

    /// Grid quads along each patch side, half of `LeafCells()`.
    std::uint32_t PatchCells() const {
        return leafCells_ / 2;
    }

    std::uint32_t Levels() const {
        return levels_;
    }

    /// World width of a level-0 node.
    float LeafSize() const {
        return static_cast<float>(leafCells_) * pixelSize_;
    }

    /// Smallest first range for which selection keeps neighbours within one level of each other
    /// and every shared edge is fully morphed on the finer side and unmorphed on the coarser side.
    ///
    /// For each level L below the root this needs the 3D diagonal of every level-L node to be at
    /// most `morphStart * range[L]`: any vertex on an edge between levels L and L+1 then lies
    /// between `range[L]` (finer side fully morphed) and the coarser level's morph start.
    /// @param morphStart Fraction of each level's distance band before morphing starts, in (0, 1).
    float MinFirstRange(float exaggeration, float morphStart) const;

    /// Per-level ranges with `range[L] = firstRange * 2^L` and morphing over the last
    /// `1 - morphStart` of each band `[range[L-1], range[L]]`. The root's range is infinite and it
    /// never morphs.
    std::vector<LodRange> Ranges(float firstRange, float morphStart) const;

    /// Appends the patches to draw for an eye at `eye` with the given `ranges`. Nodes outside
    /// `frustum` are skipped; pass nullptr to keep everything in range.
    void Select(const glm::vec3& eye, std::span<const LodRange> ranges, const Frustum* frustum,
                float exaggeration, std::vector<LodPatch>& out) const;

    /// Appends level-0 patches covering the whole raster, for drawing at full resolution.
    void SelectAll(std::vector<LodPatch>& out) const;

    /// Triangles inside the raster drawn for `patches`; quads past the raster's edge collapse to
    /// zero area in the shader and are not counted.
    std::uint64_t TriangleCount(std::span<const LodPatch> patches) const;

    /// World bounds of the node at (`x`, `y`) on `level`; empty (low > high) for nodes without
    /// valid cells.
    std::pair<glm::vec3, glm::vec3> NodeBounds(std::uint32_t level, std::uint32_t x,
                                               std::uint32_t y, float exaggeration) const;

  private:
    std::uint32_t NodesPerSide(std::uint32_t level) const {
        return 1u << (levels_ - 1 - level);
    }

    std::uint32_t NodeCells(std::uint32_t level) const {
        return leafCells_ << level;
    }

    const glm::vec2& Heights(std::uint32_t level, std::uint32_t x, std::uint32_t y) const {
        return heights_[level][static_cast<std::size_t>(y) * NodesPerSide(level) + x];
    }

    bool SelectNode(std::uint32_t level, std::uint32_t x, std::uint32_t y, const glm::vec3& eye,
                    std::span<const LodRange> ranges, const Frustum* frustum, float exaggeration,
                    std::vector<LodPatch>& out) const;
    void EmitQuarters(std::uint32_t level, std::uint32_t x, std::uint32_t y, const Frustum* frustum,
                      float exaggeration, std::vector<LodPatch>& out) const;
    void EmitChildArea(std::uint32_t level, std::uint32_t childX, std::uint32_t childY,
                       const Frustum* frustum, float exaggeration,
                       std::vector<LodPatch>& out) const;

    std::uint32_t leafCells_ = 0;
    std::uint32_t levels_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    float originX_ = 0.0f;
    float originY_ = 0.0f;
    float pixelSize_ = 1.0f;
    std::vector<std::vector<glm::vec2>> heights_;  ///< Per level, row-major (min, max) per node.
    std::vector<float> maxHeightSpan_;             ///< Per level, largest max - min of any node.
};

}  // namespace ernie
