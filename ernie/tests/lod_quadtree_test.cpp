#include "ernie/frustum.h"
#include "ernie/lod_quadtree.h"
#include "ernie/orbit_camera.h"
#include "support/test_raster.h"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

using ernie::Frustum;
using ernie::LodPatch;
using ernie::LodQuadtree;
using ernie::LodRange;
using ernie::OrbitCamera;

namespace {

constexpr float kMorphStart = 0.67f;

using ernie::testing::TestRaster;

TestRaster Terrain(std::size_t width, std::size_t height) {
    return ernie::testing::RollingTerrain(width, height);
}

TestRaster Flat(std::size_t width, std::size_t height) {
    return TestRaster(width, height, 0.0, static_cast<double>(height), 1.0);
}

/// Mirror of `grid_vertex` in terrain_common.wgsl: the world position of vertex `local` of
/// `patch` after CDLOD morphing.
glm::vec3 MorphedVertex(const TestRaster& heights, const LodPatch& patch, glm::uvec2 local,
                        const glm::vec3& eye, std::span<const LodRange> ranges,
                        float exaggeration) {
    const auto pixel = static_cast<float>(heights.cellSize);
    auto world = [&](glm::vec2 cell, float h) {
        return glm::vec3(static_cast<float>(heights.originX) + (cell.x + 0.5f) * pixel,
                         static_cast<float>(heights.originY) - (cell.y + 0.5f) * pixel,
                         h * exaggeration);
    };
    const glm::ivec2 last(static_cast<int>(heights.width) - 1,
                          static_cast<int>(heights.height) - 1);
    const auto spacing = static_cast<int>(1u << patch.level);
    const glm::ivec2 unclamped =
        glm::ivec2(patch.originX, patch.originY) + glm::ivec2(local) * spacing;
    const glm::ivec2 odd(static_cast<int>(local.x & 1u), static_cast<int>(local.y & 1u));
    const glm::ivec2 cell = glm::min(unclamped, last);
    const glm::ivec2 coarse = glm::min(unclamped - odd * spacing, last);
    const float h = heights(static_cast<std::size_t>(cell.x), static_cast<std::size_t>(cell.y));
    const float hCoarse =
        heights(static_cast<std::size_t>(coarse.x), static_cast<std::size_t>(coarse.y));

    float k = 0.0f;
    const LodRange& range = ranges[patch.level];
    if (std::isfinite(range.morphEnd)) {
        const float d = glm::distance(eye, world(glm::vec2(cell), h));
        k = std::clamp((d - range.morphStart) / (range.morphEnd - range.morphStart), 0.0f, 1.0f);
    }
    return world(glm::mix(glm::vec2(cell), glm::vec2(coarse), k), h + (hCoarse - h) * k);
}

/// Times each grid quad of the raster is covered by `patches`, row-major.
std::vector<int> Coverage(const TestRaster& heights, const LodQuadtree& tree,
                          std::span<const LodPatch> patches) {
    const std::size_t quadsX = heights.width - 1;
    const std::size_t quadsY = heights.height - 1;
    std::vector<int> covered(quadsX * quadsY, 0);
    const std::size_t side = tree.PatchCells();
    for (const LodPatch& patch : patches) {
        const std::size_t extent = side << patch.level;
        for (std::size_t row = patch.originY; row < std::min(patch.originY + extent, quadsY);
             ++row) {
            for (std::size_t col = patch.originX; col < std::min(patch.originX + extent, quadsX);
                 ++col) {
                ++covered[row * quadsX + col];
            }
        }
    }
    return covered;
}

/// Morphed vertices along the part of `patch`'s edge at grid line `fixed` (a column when
/// `vertical`) between `begin` and `end`, with consecutive duplicates removed.
std::vector<glm::vec3> EdgePolyline(const TestRaster& heights, const LodQuadtree& tree,
                                    const LodPatch& patch, bool vertical, std::uint32_t fixed,
                                    std::uint32_t begin, std::uint32_t end, const glm::vec3& eye,
                                    std::span<const LodRange> ranges, float exaggeration) {
    const std::uint32_t spacing = 1u << patch.level;
    const std::uint32_t origin = vertical ? patch.originY : patch.originX;
    const std::uint32_t across =
        ((vertical ? patch.originX : patch.originY) == fixed) ? 0u : tree.PatchCells();
    std::vector<glm::vec3> points;
    for (std::uint32_t along = (begin - origin) / spacing; origin + along * spacing <= end;
         ++along) {
        const glm::uvec2 local = vertical ? glm::uvec2(across, along) : glm::uvec2(along, across);
        const glm::vec3 p = MorphedVertex(heights, patch, local, eye, ranges, exaggeration);
        if (points.empty() || glm::distance(points.back(), p) > 1e-3f) {
            points.push_back(p);
        }
    }
    return points;
}

struct SeamReport {
    int sharedEdges = 0;
    int levelChanges = 0;
    int cracks = 0;
    int levelJumps = 0;
};

/// Compares the morphed geometry of every edge shared by two patches from both sides.
SeamReport CheckSeams(const TestRaster& heights, const LodQuadtree& tree,
                      std::span<const LodPatch> patches, const glm::vec3& eye,
                      std::span<const LodRange> ranges, float exaggeration) {
    SeamReport report;
    const std::uint32_t side = tree.PatchCells();
    for (const LodPatch& a : patches) {
        for (const LodPatch& b : patches) {
            const std::uint32_t extentA = side << a.level;
            const std::uint32_t extentB = side << b.level;
            for (const bool vertical : {true, false}) {
                const std::uint32_t aFar = (vertical ? a.originX : a.originY) + extentA;
                const std::uint32_t bNear = vertical ? b.originX : b.originY;
                if (aFar != bNear) {
                    continue;
                }
                const std::uint32_t aStart = vertical ? a.originY : a.originX;
                const std::uint32_t bStart = vertical ? b.originY : b.originX;
                const std::uint32_t begin = std::max(aStart, bStart);
                const std::uint32_t end = std::min(aStart + extentA, bStart + extentB);
                if (begin >= end) {
                    continue;
                }
                ++report.sharedEdges;
                const int step = std::abs(static_cast<int>(a.level) - static_cast<int>(b.level));
                report.levelChanges += step == 1 ? 1 : 0;
                report.levelJumps += step > 1 ? 1 : 0;
                const auto fromA = EdgePolyline(heights, tree, a, vertical, aFar, begin, end, eye,
                                                ranges, exaggeration);
                const auto fromB = EdgePolyline(heights, tree, b, vertical, bNear, begin, end, eye,
                                                ranges, exaggeration);
                bool same = fromA.size() == fromB.size();
                for (std::size_t i = 0; same && i < fromA.size(); ++i) {
                    same = glm::distance(fromA[i], fromB[i]) <= 1e-3f;
                }
                report.cracks += same ? 0 : 1;
            }
        }
    }
    return report;
}

std::vector<glm::vec3> TestEyes(const TestRaster& heights) {
    const auto x0 = static_cast<float>(heights.originX);
    const auto y0 = static_cast<float>(heights.originY);
    const auto w = static_cast<float>(static_cast<double>(heights.width) * heights.cellSize);
    const auto h = static_cast<float>(static_cast<double>(heights.height) * heights.cellSize);
    return {{x0 + 0.5f * w, y0 - 0.5f * h, 5.0f},   {x0 + 0.1f * w, y0 - 0.2f * h, 40.0f},
            {x0 + 0.9f * w, y0 - 0.7f * h, 1.0f},   {x0 - 0.3f * w, y0 + 0.2f * h, 80.0f},
            {x0 + 0.37f * w, y0 - 0.61f * h, 0.0f}, {x0 + 0.5f * w, y0 - 0.5f * h, 4.0f * w}};
}

}  // namespace

TEST(LodQuadtree, RejectsInvalidLeavesAndRasters) {
    EXPECT_THROW(LodQuadtree(Flat(9, 9), 3), std::invalid_argument);
    EXPECT_THROW(LodQuadtree(Flat(9, 9), 6), std::invalid_argument);
    EXPECT_THROW(LodQuadtree(Flat(9, 9), 2), std::invalid_argument);
    EXPECT_THROW(LodQuadtree(Flat(1, 9), 4), std::invalid_argument);
    EXPECT_THROW(LodQuadtree(Flat((std::size_t{4} << LodQuadtree::kMaxLevels) + 2, 2), 4),
                 std::invalid_argument);
    EXPECT_TRUE(LodQuadtree().Empty());
}

TEST(LodQuadtree, CountsLevelsFromTheLongerSide) {
    EXPECT_EQ(LodQuadtree(Flat(65, 33), 64).Levels(), 1u);
    EXPECT_EQ(LodQuadtree(Flat(66, 33), 64).Levels(), 2u);
    EXPECT_EQ(LodQuadtree(Flat(9, 257), 8).Levels(), 6u);
    const LodQuadtree tree(Flat(257, 257), 16);
    EXPECT_EQ(tree.LeafCells(), 16u);
    EXPECT_EQ(tree.PatchCells(), 8u);
    EXPECT_FLOAT_EQ(tree.LeafSize(), 16.0f);
}

TEST(LodQuadtree, NodeBoundsContainEveryValidCellAndSkipInvalidOnes) {
    TestRaster heights = Terrain(100, 70);
    heights(3, 4) = std::numeric_limits<float>::quiet_NaN();
    heights(5, 6) = -9999.0f;
    heights.noData = -9999.0f;
    for (std::size_t row = 32; row <= 48; ++row) {
        for (std::size_t col = 64; col < 80; ++col) {
            heights(col, row) = std::numeric_limits<float>::infinity();
        }
    }
    const LodQuadtree tree(heights, 16);
    const TestRaster& t = heights;
    for (std::uint32_t level = 0; level < tree.Levels(); ++level) {
        const std::uint32_t side = 1u << (tree.Levels() - 1 - level);
        const std::uint32_t cells = 16u << level;
        for (std::uint32_t y = 0; y < side; ++y) {
            for (std::uint32_t x = 0; x < side; ++x) {
                const auto [low, high] = tree.NodeBounds(level, x, y, 2.0f);
                for (std::size_t row = y * cells;
                     row <= std::min<std::size_t>(y * cells + cells, 69); ++row) {
                    for (std::size_t col = x * cells;
                         col <= std::min<std::size_t>(x * cells + cells, 99); ++col) {
                        const float h = heights(col, row);
                        if (!std::isfinite(h) || h == -9999.0f) {
                            continue;
                        }
                        const glm::vec2 centre(heights.CellCenter(col, row).x - t.originX,
                                               heights.CellCenter(col, row).y - t.originY);
                        EXPECT_LE(low.z, 2.0f * h);
                        EXPECT_GE(high.z, 2.0f * h);
                        EXPECT_LE(low.x - static_cast<float>(t.originX), centre.x + 1e-3f);
                        EXPECT_GE(high.y - static_cast<float>(t.originY), centre.y - 1e-3f);
                    }
                }
            }
        }
    }
    const auto [low, high] = tree.NodeBounds(0, 0, 0, 1.0f);
    EXPECT_GT(low.z, -9000.0f) << "nodata ignored";
    EXPECT_TRUE(std::isfinite(high.z)) << "NaN ignored";
}

TEST(LodQuadtree, NodesWithoutValidCellsAreNeverDrawn) {
    TestRaster heights = Flat(33, 33);
    for (std::size_t row = 0; row <= 8; ++row) {
        for (std::size_t col = 0; col <= 8; ++col) {
            heights(col, row) = std::numeric_limits<float>::quiet_NaN();
        }
    }
    const LodQuadtree tree(heights, 8);
    const auto [low, high] = tree.NodeBounds(0, 0, 0, 1.0f);
    EXPECT_GT(low.z, high.z);

    std::vector<LodPatch> all;
    tree.SelectAll(all);
    EXPECT_EQ(all.size(), 15u * 4u);
    std::vector<LodPatch> selected;
    tree.Select(glm::vec3(0.0f, 33.0f, 1.0f), tree.Ranges(30.0f, kMorphStart), nullptr, 1.0f,
                selected);
    for (const LodPatch& patch : selected) {
        EXPECT_FALSE(patch.level == 0 && patch.originX < 8 && patch.originY < 8);
    }
}

TEST(LodQuadtree, RangesDoubleAndMorphOverTheEndOfEachBand) {
    const LodQuadtree tree(Flat(257, 257), 16);
    const std::vector<LodRange> ranges = tree.Ranges(100.0f, 0.75f);
    ASSERT_EQ(ranges.size(), tree.Levels());
    EXPECT_FLOAT_EQ(ranges[0].morphStart, 75.0f);
    EXPECT_FLOAT_EQ(ranges[0].morphEnd, 100.0f);
    EXPECT_FLOAT_EQ(ranges[1].morphStart, 175.0f);
    EXPECT_FLOAT_EQ(ranges[1].morphEnd, 200.0f);
    EXPECT_FLOAT_EQ(ranges[3].morphEnd, 800.0f);
    EXPECT_TRUE(std::isinf(ranges.back().morphStart));
    EXPECT_TRUE(std::isinf(ranges.back().morphEnd));
}

TEST(LodQuadtree, MinFirstRangeBoundsEveryNodeDiagonal) {
    EXPECT_EQ(LodQuadtree(Flat(33, 33), 32).MinFirstRange(1.0f, kMorphStart), 0.0f);
    EXPECT_NEAR(LodQuadtree(Flat(65, 65), 16).MinFirstRange(1.0f, 0.5f),
                16.0f * std::sqrt(2.0f) / 0.5f, 1e-3f);

    const TestRaster heights = Terrain(129, 129);
    const LodQuadtree tree(heights, 8);
    const float exaggeration = 5.0f;
    const float range = tree.MinFirstRange(exaggeration, kMorphStart);
    EXPECT_GT(range, LodQuadtree(Flat(129, 129), 8).MinFirstRange(exaggeration, kMorphStart))
        << "relief lengthens the diagonals";
    for (std::uint32_t level = 0; level + 1 < tree.Levels(); ++level) {
        const std::uint32_t side = 1u << (tree.Levels() - 1 - level);
        for (std::uint32_t y = 0; y < side; ++y) {
            for (std::uint32_t x = 0; x < side; ++x) {
                const auto [low, high] = tree.NodeBounds(level, x, y, exaggeration);
                EXPECT_LE(glm::distance(low, high),
                          kMorphStart * range * static_cast<float>(1u << level) * 1.0001f);
            }
        }
    }
}

TEST(LodQuadtree, SelectAllCoversTheRasterOnceAtFullResolution) {
    for (const auto [w, h] : {std::pair<std::size_t, std::size_t>{65, 33}, {100, 37}, {2, 2}}) {
        const TestRaster heights = Flat(w, h);
        const LodQuadtree tree(heights, 16);
        std::vector<LodPatch> patches;
        tree.SelectAll(patches);
        for (const LodPatch& patch : patches) {
            EXPECT_EQ(patch.level, 0u);
        }
        for (const int count : Coverage(heights, tree, patches)) {
            EXPECT_EQ(count, 1);
        }
        EXPECT_EQ(tree.TriangleCount(patches), 2u * (w - 1) * (h - 1));
    }
    std::vector<LodPatch> none;
    LodQuadtree().SelectAll(none);
    LodQuadtree().Select(glm::vec3(0.0f), {}, nullptr, 1.0f, none);
    EXPECT_TRUE(none.empty());
}

TEST(LodQuadtree, SelectionTilesTheRasterWithoutGapsOrOverlaps) {
    for (const auto [w, h] : {std::pair<std::size_t, std::size_t>{257, 257}, {200, 131}}) {
        const TestRaster heights = Terrain(w, h);
        const LodQuadtree tree(heights, 8);
        const auto ranges = tree.Ranges(tree.MinFirstRange(1.0f, kMorphStart), kMorphStart);
        for (const glm::vec3& eye : TestEyes(heights)) {
            std::vector<LodPatch> patches;
            tree.Select(eye, ranges, nullptr, 1.0f, patches);
            for (const int count : Coverage(heights, tree, patches)) {
                ASSERT_EQ(count, 1);
            }
            EXPECT_LE(tree.TriangleCount(patches), 2u * (w - 1) * (h - 1));
        }
    }
}

TEST(LodQuadtree, DetailFallsWithDistance) {
    const TestRaster heights = Terrain(257, 257);
    const LodQuadtree tree(heights, 8);
    const auto ranges = tree.Ranges(tree.MinFirstRange(1.0f, kMorphStart), kMorphStart);

    std::vector<LodPatch> far;
    tree.Select(glm::vec3(257.0f, 0.0f, 1e6f), ranges, nullptr, 1.0f, far);
    ASSERT_EQ(far.size(), 4u);
    for (const LodPatch& patch : far) {
        EXPECT_EQ(patch.level, tree.Levels() - 1);
    }

    const glm::vec3 eye(heights.CellCenter(10, 20).x, heights.CellCenter(10, 20).y, 0.0f);
    std::vector<LodPatch> near;
    tree.Select(eye, ranges, nullptr, 1.0f, near);
    EXPECT_GT(near.size(), far.size());
    const auto under = std::find_if(near.begin(), near.end(), [](const LodPatch& p) {
        return p.originX <= 10 && 10 < p.originX + 4 && p.originY <= 20 && 20 < p.originY + 4;
    });
    ASSERT_NE(under, near.end());
    EXPECT_EQ(under->level, 0u);
    EXPECT_LT(tree.TriangleCount(near), 2u * 256u * 256u);
}

TEST(LodQuadtree, NeighboursDifferByOneLevelAndSharedEdgesMatchExactly) {
    for (const float exaggeration : {1.0f, 4.0f}) {
        for (const auto [w, h] : {std::pair<std::size_t, std::size_t>{257, 257}, {200, 131}}) {
            const TestRaster heights = Terrain(w, h);
            const LodQuadtree tree(heights, 8);
            const float first = tree.MinFirstRange(exaggeration, kMorphStart);
            int levelChanges = 0;
            for (const float scale : {1.0f, 1.7f}) {
                const auto ranges = tree.Ranges(scale * first, kMorphStart);
                for (const glm::vec3& eye : TestEyes(heights)) {
                    std::vector<LodPatch> patches;
                    tree.Select(eye, ranges, nullptr, exaggeration, patches);
                    const SeamReport report =
                        CheckSeams(heights, tree, patches, eye, ranges, exaggeration);
                    EXPECT_GT(report.sharedEdges, 0);
                    EXPECT_EQ(report.levelJumps, 0);
                    EXPECT_EQ(report.cracks, 0);
                    levelChanges += report.levelChanges;
                }
            }
            EXPECT_GT(levelChanges, 0) << "no edges between levels were checked";
        }
    }
}

TEST(LodQuadtree, RangesBelowTheMinimumCanCrack) {
    const TestRaster heights = Terrain(257, 257);
    const LodQuadtree tree(heights, 8);
    const auto ranges = tree.Ranges(0.3f * tree.MinFirstRange(1.0f, kMorphStart), kMorphStart);
    int cracks = 0;
    for (const glm::vec3& eye : TestEyes(heights)) {
        std::vector<LodPatch> patches;
        tree.Select(eye, ranges, nullptr, 1.0f, patches);
        cracks += CheckSeams(heights, tree, patches, eye, ranges, 1.0f).cracks;
    }
    EXPECT_GT(cracks, 0) << "the seam check would not notice a bad range";
}

TEST(LodQuadtree, FullyMorphedPatchMatchesTheCoarserLevel) {
    const TestRaster heights = Terrain(65, 65);
    const LodQuadtree tree(heights, 8);
    const std::vector<LodRange> fine = {
        {-2.0f, -1.0f}, {-2.0f, -1.0f}, {-2.0f, -1.0f}, {-2.0f, -1.0f}};
    const std::vector<LodRange> still = {{1e9f, 2e9f}, {1e9f, 2e9f}, {1e9f, 2e9f}, {1e9f, 2e9f}};
    const LodPatch patch{8, 16, 1, 0};
    const LodPatch coarser{8, 16, 2, 0};
    for (std::uint32_t j = 0; j <= 4; ++j) {
        for (std::uint32_t i = 0; i <= 4; ++i) {
            const glm::vec3 morphed =
                MorphedVertex(heights, patch, {i, j}, glm::vec3(0.0f), fine, 1.0f);
            const glm::vec3 target =
                MorphedVertex(heights, coarser, {i / 2, j / 2}, glm::vec3(0.0f), still, 1.0f);
            EXPECT_NEAR(glm::distance(morphed, target), 0.0f, 1e-4f) << i << ", " << j;
        }
    }
}

TEST(LodQuadtree, FrustumCullingKeepsOnlyVisiblePatches) {
    const TestRaster heights = Terrain(257, 257);
    const LodQuadtree tree(heights, 8);
    const auto ranges = tree.Ranges(tree.MinFirstRange(1.0f, kMorphStart), kMorphStart);

    OrbitCamera camera;
    camera.target = glm::vec3(heights.CellCenter(128, 128).x, heights.CellCenter(128, 128).y, 0.0f);
    camera.distance = 150.0f;
    camera.pitch = 0.3f;
    const auto view = camera.MakeView(1.0f);
    const Frustum frustum = Frustum::FromMatrix(view.ViewProjection());

    std::vector<LodPatch> all;
    tree.Select(view.eye, ranges, nullptr, 1.0f, all);
    std::vector<LodPatch> visible;
    tree.Select(view.eye, ranges, &frustum, 1.0f, visible);
    EXPECT_LT(visible.size(), all.size());
    EXPECT_FALSE(visible.empty());
    for (const LodPatch& patch : visible) {
        EXPECT_NE(std::find(all.begin(), all.end(), patch), all.end());
    }

    OrbitCamera away = camera;
    away.target = glm::vec3(1e5f, 1e5f, 0.0f);
    away.distance = 10.0f;
    const auto awayView = away.MakeView(1.0f);
    const Frustum awayFrustum = Frustum::FromMatrix(awayView.ViewProjection());
    std::vector<LodPatch> none;
    tree.Select(view.eye, ranges, &awayFrustum, 1.0f, none);
    EXPECT_TRUE(none.empty());
}

TEST(LodQuadtree, TriangleCountSkipsQuadsPastTheRaster) {
    const LodQuadtree tree(Flat(21, 13), 8);
    const std::vector<LodPatch> patches = {
        {0, 0, 0, 0}, {16, 8, 0, 0}, {16, 8, 1, 0}, {24, 0, 0, 0}};
    EXPECT_EQ(tree.TriangleCount(std::span(patches).subspan(0, 1)), 2u * 4u * 4u);
    EXPECT_EQ(tree.TriangleCount(std::span(patches).subspan(1, 1)), 2u * 4u * 4u);
    EXPECT_EQ(tree.TriangleCount(std::span(patches).subspan(2, 1)), 2u * 2u * 2u);
    EXPECT_EQ(tree.TriangleCount(std::span(patches).subspan(3, 1)), 0u);
}
