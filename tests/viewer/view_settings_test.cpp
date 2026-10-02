#include "view_settings.h"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

using terraingen::viewer::SunDirection;

TEST(SunDirection, UsesCompassAzimuthAndElevation) {
    const glm::vec3 north = SunDirection(0.0f, 0.0f);
    EXPECT_NEAR(north.x, 0.0f, 1e-6f);
    EXPECT_NEAR(north.y, 1.0f, 1e-6f);
    EXPECT_NEAR(north.z, 0.0f, 1e-6f);

    const glm::vec3 east = SunDirection(90.0f, 0.0f);
    EXPECT_NEAR(east.x, 1.0f, 1e-6f);
    EXPECT_NEAR(east.y, 0.0f, 1e-6f);

    const glm::vec3 overhead = SunDirection(123.0f, 90.0f);
    EXPECT_NEAR(overhead.z, 1.0f, 1e-6f);

    EXPECT_NEAR(glm::length(SunDirection(315.0f, 35.0f)), 1.0f, 1e-6f);
}
