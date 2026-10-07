#include "terraingen/bodies/radii.h"

#include <gtest/gtest.h>

using terraingen::Body;

TEST(MeanRadius, IsNasasVolumetricMeanRadius) {
    EXPECT_EQ(terraingen::MeanRadius(Body::Moon), 1737400.0);
    EXPECT_EQ(terraingen::MeanRadius(Body::Mars), 3389500.0);
    EXPECT_EQ(terraingen::MeanRadius(Body::Mercury), 2439700.0);
    EXPECT_EQ(terraingen::BodySphere(Body::Mars), terraingen::Surface::Sphere(3389500.0));
}

TEST(BodyNamed, KnowsEachPreset) {
    EXPECT_EQ(terraingen::BodyNamed("moon"), Body::Moon);
    EXPECT_EQ(terraingen::BodyNamed("mars"), Body::Mars);
    EXPECT_EQ(terraingen::BodyNamed("mercury"), Body::Mercury);
    EXPECT_FALSE(terraingen::BodyNamed("venus"));
}
