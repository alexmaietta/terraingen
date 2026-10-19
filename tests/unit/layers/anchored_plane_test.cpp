#include "terraingen/layers/anchored_plane.h"

#include <gtest/gtest.h>

TEST(AnchoredPlane, IsThePlaneItselfOnAPlane) {
    const terraingen::AnchoredPlane plane({}, {5.0, 5.0});
    const auto q = plane.Project(3.0, -4.0);
    ASSERT_TRUE(q);
    EXPECT_EQ(*q, (terraingen::Vec2d{3.0, -4.0}));
}

TEST(AnchoredPlane, IsTheAnchorsTangentFrameOnABody) {
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    const terraingen::Vec2d anchor{10.0, 20.0};
    const terraingen::AnchoredPlane plane(moon, anchor);
    const auto atAnchor = plane.Project(anchor.x, anchor.y);
    ASSERT_TRUE(atAnchor);
    EXPECT_NEAR(atAnchor->x, 0.0, 1e-6);
    EXPECT_NEAR(atAnchor->y, 0.0, 1e-6);
    const auto near = plane.Project(10.001, 20.0);
    ASSERT_TRUE(near);
    EXPECT_EQ(*near, moon.Offset(anchor, {10.001, 20.0}));
    EXPECT_FALSE(plane.Project(-170.0, -20.0)) << "nothing on the far hemisphere";
}
