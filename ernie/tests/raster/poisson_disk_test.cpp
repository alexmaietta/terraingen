#include "ernie/raster/poisson_disk.h"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using ernie::PoissonDisk;

namespace {

float MinimumSpacing(const std::vector<glm::vec2>& points) {
    float spacing = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < points.size(); ++i) {
        for (std::size_t j = i + 1; j < points.size(); ++j) {
            spacing = std::fmin(spacing, glm::length(points[i] - points[j]));
        }
    }
    return spacing;
}

}  // namespace

TEST(PoissonDisk, ReturnsTheRequestedCountInsideTheUnitDisk) {
    const auto points = PoissonDisk(16);
    ASSERT_EQ(points.size(), 16u);
    for (const glm::vec2& p : points) {
        EXPECT_LE(glm::length(p), 1.0f);
    }
    EXPECT_TRUE(PoissonDisk(0).empty());
}

TEST(PoissonDisk, IsDeterministicPerSeed) {
    EXPECT_EQ(PoissonDisk(16, 3), PoissonDisk(16, 3));
    EXPECT_NE(PoissonDisk(16, 3), PoissonDisk(16, 4));
}

TEST(PoissonDisk, KeepsPointsWellSeparated) {
    for (const std::uint32_t seed : {0u, 1u, 2u, 3u}) {
        EXPECT_GT(MinimumSpacing(PoissonDisk(16, seed)), 0.3f) << "seed " << seed;
    }
}

TEST(PoissonDisk, CoversTheDiskEvenly) {
    glm::vec2 centroid(0.0f);
    for (const glm::vec2& p : PoissonDisk(32)) {
        centroid += p / 32.0f;
    }
    EXPECT_LT(glm::length(centroid), 0.15f);
}
