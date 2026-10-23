#include "terraingen/util/plane_fit.h"

#include <gtest/gtest.h>

using terraingen::Plane;
using terraingen::PlaneFit;

TEST(PlaneFit, FitsAPlaneAndFallsBackToTheMean) {
    PlaneFit fit;
    EXPECT_TRUE(fit.Empty());
    for (const double dx : {-1.0, 0.0, 1.0}) {
        for (const double dy : {-1.0, 0.0, 1.0}) {
            fit.Add(dx, dy, 3.0 + 2.0 * dx - dy);
        }
    }
    EXPECT_FALSE(fit.Empty());
    EXPECT_DOUBLE_EQ(fit.Mean(), 3.0);
    const Plane plane = fit.Solve();
    EXPECT_NEAR(plane.a, 3.0, 1e-12);
    EXPECT_NEAR(plane.b, 2.0, 1e-12);
    EXPECT_NEAR(plane.c, -1.0, 1e-12);
    PlaneFit line;
    line.Add(0.0, 0.0, 1.0);
    line.Add(1.0, 0.0, 3.0);
    line.Add(2.0, 0.0, 5.0);
    EXPECT_EQ(line.Solve(), (Plane{3.0, 0.0, 0.0})) << "points on a line fix no plane";
}

TEST(PlaneFit, SolvesFewerThanThreePointsAsTheirMean) {
    PlaneFit fit;
    fit.Add(0.0, 0.0, 1.0);
    fit.Add(1.0, 1.0, 5.0);
    EXPECT_EQ(fit.Solve(), (Plane{3.0, 0.0, 0.0}));
}
