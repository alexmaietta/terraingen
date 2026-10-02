#include "ernie/raster/mesh_part.h"

#include <gtest/gtest.h>

#include <vector>

using ernie::TriangulateFaces;

TEST(TriangulateFaces, FansPolygonsAndKeepsTriangles) {
    const std::vector<std::int64_t> counts{3, 4, 5};
    const std::vector<std::int64_t> indices{0, 1, 2, 0, 2, 3, 4, 1, 2, 3, 4, 5};
    EXPECT_EQ(TriangulateFaces(counts, indices, 6),
              (std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3, 0, 3, 4, 1, 2, 3, 1, 3, 4, 1, 4, 5}));
    EXPECT_TRUE(TriangulateFaces({}, {}, 0).empty());
}

TEST(TriangulateFaces, RejectsMalformedTopology) {
    const std::vector<std::int64_t> two{2};
    const std::vector<std::int64_t> three{3};
    const std::vector<std::int64_t> triangle{0, 1, 2};
    const std::vector<std::int64_t> four{0, 1, 2, 3};
    const std::vector<std::int64_t> negative{0, -1, 2};
    EXPECT_THROW(TriangulateFaces(two, std::vector<std::int64_t>{0, 1}, 3), std::invalid_argument);
    EXPECT_THROW(TriangulateFaces(std::vector<std::int64_t>{4}, triangle, 3),
                 std::invalid_argument);
    EXPECT_THROW(TriangulateFaces(three, four, 4), std::invalid_argument);
    EXPECT_THROW(TriangulateFaces(three, triangle, 2), std::invalid_argument);
    EXPECT_THROW(TriangulateFaces(three, negative, 3), std::invalid_argument);
}
